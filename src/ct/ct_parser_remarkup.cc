/*
 * ct_parser_remarkup.cc
 *
 * Remarkup (Phabricator / Phorge markup) to CherryTree rich text.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_parser_remarkup.h"
#include "ct_config.h"
#include "ct_const.h"
#include "ct_imports.h"
#include "ct_storage_xml.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"

#include <algorithm>
#include <cctype>

namespace {

const char* const REF_UNRESOLVED_COLOUR = "#8a8a8a";
const char* const HIGHLIGHT_COLOUR      = "#fff3a0";
const char* const QUOTE_COLOUR          = "#7a7a7a";
const char* const CALLOUT_NOTE_COLOUR      = "#2e6fbf";
const char* const CALLOUT_WARNING_COLOUR   = "#c77800";
const char* const CALLOUT_IMPORTANT_COLOUR = "#c0392b";

Glib::ustring rtrim(const Glib::ustring& s)
{
    Glib::ustring::size_type end = s.size();
    while (end > 0 && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r')) --end;
    return s.substr(0, end);
}

Glib::ustring ltrim(const Glib::ustring& s)
{
    Glib::ustring::size_type start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
    return s.substr(start);
}

Glib::ustring trim(const Glib::ustring& s) { return ltrim(rtrim(s)); }

std::size_t leading_spaces(const Glib::ustring& s)
{
    std::size_t n = 0;
    while (n < s.size() && s[n] == ' ') ++n;
    return n;
}

bool all_of_char(const Glib::ustring& s, const char ch, const std::size_t min_len)
{
    if (s.size() < min_len) return false;
    for (const gunichar c : s) if (c != static_cast<gunichar>(ch)) return false;
    return true;
}

bool is_word_char(const gunichar c)
{
    return g_unichar_isalnum(c) || c == '_';
}

bool is_url_start(const Glib::ustring& text, const Glib::ustring::size_type pos)
{
    static const char* const prefixes[] = {"http://", "https://", "ftp://", "mailto:"};
    for (const char* prefix : prefixes) {
        if (text.compare(pos, std::strlen(prefix), prefix) == 0) return true;
    }
    return false;
}

struct ListItem {
    int level{0};
    bool ordered{false};
    int checkbox{-1}; // -1 none, 0 unchecked, 1 checked
    Glib::ustring text;
};

// "- item", "* item", "# item", "-- nested", "  - nested", "1. item", "1) item", "- [ ] task"
bool parse_list_item(const Glib::ustring& line, ListItem& out)
{
    const std::size_t indent = leading_spaces(line);
    Glib::ustring rest = line.substr(indent);
    if (rest.empty()) return false;
    int level = static_cast<int>(indent / 2);
    const gunichar marker = rest[0];
    if (marker == '-' || marker == '*' || marker == '#') {
        std::size_t n = 0;
        while (n < rest.size() && rest[n] == marker) ++n;
        if (n >= rest.size() || rest[n] != ' ') return false;
        level += static_cast<int>(n) - 1;
        out.ordered = (marker == '#');
        rest = rest.substr(n + 1);
    }
    else if (g_unichar_isdigit(marker)) {
        std::size_t n = 0;
        while (n < rest.size() && g_unichar_isdigit(rest[n])) ++n;
        if (n + 1 >= rest.size() || (rest[n] != '.' && rest[n] != ')') || rest[n+1] != ' ') return false;
        out.ordered = true;
        rest = rest.substr(n + 2);
    }
    else {
        return false;
    }
    out.level = level;
    out.checkbox = -1;
    if (rest.size() >= 3 && rest[0] == '[' && rest[2] == ']' && (rest.size() == 3 || rest[3] == ' ')) {
        const gunichar c = rest[1];
        if (c == ' ') { out.checkbox = 0; rest = ltrim(rest.substr(3)); }
        else if (c == 'x' || c == 'X') { out.checkbox = 1; rest = ltrim(rest.substr(3)); }
    }
    out.text = rest;
    return true;
}

bool is_table_line(const Glib::ustring& line)
{
    const Glib::ustring t = ltrim(line);
    return !t.empty() && t[0] == '|';
}

bool is_fence_line(const Glib::ustring& line, Glib::ustring* pRest = nullptr)
{
    const Glib::ustring t = ltrim(line);
    if (t.compare(0, 3, "```") == 0 || t.compare(0, 3, "~~~") == 0) {
        if (pRest) *pRest = trim(t.substr(3));
        return true;
    }
    return false;
}

} // namespace

CtRemarkupParser::CtRemarkupParser(const CtConfig* pCtConfig)
 : _pCtConfig{pCtConfig}
{
    wipe();
}

CtRemarkupParser::~CtRemarkupParser() = default;

void CtRemarkupParser::wipe()
{
    _pDocument = std::make_unique<xmlpp::Document>();
    xmlpp::Element* pRoot = _pDocument->create_root_node("root");
    _pSlot = pRoot->add_child("slot");
    _charOffset = 0;
    _paragraph.clear();
}

xmlpp::Node* CtRemarkupParser::root_node() const
{
    return _pDocument ? _pDocument->get_root_node() : nullptr;
}

std::string CtRemarkupParser::to_string() const
{
    if (not _pDocument) return std::string{};
    return _pDocument->write_to_string();
}

bool CtRemarkupParser::is_object_reference(const Glib::ustring& word, Glib::ustring* pNormalised)
{
    if (word.empty()) return false;
    Glib::ustring w = word;
    if (w[0] == '{' && w.size() > 2 && w[w.size()-1] == '}') {
        w = w.substr(1, w.size() - 2);
    }
    if (w.empty()) return false;
    // @user, #project
    if ((w[0] == '@' || w[0] == '#') && w.size() > 1) {
        for (std::size_t i = 1; i < w.size(); ++i) {
            if (!is_word_char(w[i]) && w[i] != '-' && w[i] != '.') return false;
        }
        if (!g_unichar_isalpha(w[1]) && w[1] != '_') return false;
        if (pNormalised) *pNormalised = w;
        return true;
    }
    // T123, D45, P6, M7, F8, V9, Z10, Q11, E12, optionally followed by #anchor
    static const std::string letters = "TDPMFVZQEWJKL";
    if (letters.find(static_cast<char>(w[0])) == std::string::npos) return false;
    std::size_t i = 1;
    while (i < w.size() && g_unichar_isdigit(w[i])) ++i;
    if (i == 1) return false;
    if (i < w.size() && w[i] != '#') return false;
    if (pNormalised) *pNormalised = w.substr(0, i);
    return true;
}

void CtRemarkupParser::_emit_text(const Glib::ustring& text, const Style& style)
{
    if (text.empty()) return;
    xmlpp::Element* pEl = _pSlot->add_child("rich_text");
    if (style.bold)      pEl->set_attribute(CtConst::TAG_WEIGHT, CtConst::TAG_PROP_VAL_HEAVY);
    if (style.italic)    pEl->set_attribute(CtConst::TAG_STYLE, CtConst::TAG_PROP_VAL_ITALIC);
    if (style.mono)      pEl->set_attribute(CtConst::TAG_FAMILY, CtConst::TAG_PROP_VAL_MONOSPACE);
    if (style.strike)    pEl->set_attribute(CtConst::TAG_STRIKETHROUGH, CtConst::TAG_PROP_VAL_TRUE);
    if (style.underline) pEl->set_attribute(CtConst::TAG_UNDERLINE, CtConst::TAG_PROP_VAL_SINGLE);
    if (style.highlight) pEl->set_attribute(CtConst::TAG_BACKGROUND, HIGHLIGHT_COLOUR);
    if (!style.foreground.empty()) pEl->set_attribute(CtConst::TAG_FOREGROUND, style.foreground);
    if (!style.scale.empty())      pEl->set_attribute(CtConst::TAG_SCALE, style.scale);
    if (!style.link.empty())       pEl->set_attribute(CtConst::TAG_LINK, style.link);
    pEl->add_child_text(text);
    _charOffset += static_cast<int>(text.size());
}

void CtRemarkupParser::_emit_newline()
{
    _emit_text(CtConst::CHAR_NEWLINE, Style{});
}

void CtRemarkupParser::_emit_codebox(const Glib::ustring& language, const Glib::ustring& code)
{
    xmlpp::Element* pCodebox = CtXML::codebox_to_xml(_pSlot,
                                                     CtConst::TAG_PROP_VAL_LEFT,
                                                     _charOffset,
                                                     static_cast<int>(_pCtConfig->codeboxWidth),
                                                     static_cast<int>(_pCtConfig->codeboxHeight),
                                                     _pCtConfig->codeboxWidthPixels,
                                                     language.empty() ? Glib::ustring{CtConst::PLAIN_TEXT_ID} : language,
                                                     false/*highlight_brackets*/,
                                                     false/*show_line_numbers*/);
    pCodebox->add_child_text(code);
    ++_charOffset;
    _emit_newline();
}

void CtRemarkupParser::_emit_table(const std::vector<std::vector<Glib::ustring>>& rows)
{
    if (rows.empty()) return;
    const bool is_light = rows.size() * rows.front().size() > static_cast<unsigned>(_pCtConfig->tableCellsGoLight);
    CtXmlHelper::table_to_xml(_pSlot, rows, _charOffset, CtConst::TAG_PROP_VAL_LEFT, _pCtConfig->tableColWidthDefault, "", is_light);
    ++_charOffset;
    _emit_newline();
}

void CtRemarkupParser::_emit_object_reference(const Glib::ustring& reference, const Glib::ustring& display, Style style)
{
    Glib::ustring normalised;
    if (!is_object_reference(reference, &normalised)) {
        _emit_text(display, style);
        return;
    }
    std::optional<gint64> nodeId;
    if (_refResolver) nodeId = _refResolver(normalised);
    if (nodeId) {
        style.link = CtConst::LINK_TYPE_NODE + CtConst::CHAR_SPACE + std::to_string(*nodeId);
        _emit_text(display, style);
    }
    else {
        style.mono = true;
        style.foreground = REF_UNRESOLVED_COLOUR;
        _emit_text(display, style);
    }
}

// Inline formatting: **bold** //italic// ##mono## `mono` !!highlight!! ~~strike~~ __underline__
// links: [[url | text]] [[url]] [text](url) <url> bare urls, object references
void CtRemarkupParser::_emit_inline(const Glib::ustring& text, Style style)
{
    Glib::ustring pending;
    auto flush = [&]{ _emit_text(pending, style); pending.clear(); };

    const Glib::ustring::size_type len = text.size();
    Glib::ustring::size_type i = 0;
    auto prev_is_boundary = [&](Glib::ustring::size_type pos) {
        return pos == 0 || !is_word_char(text[pos-1]);
    };
    auto toggle = [&](const char* delim, bool Style::*flag) -> bool {
        const std::size_t dlen = std::strlen(delim);
        if (text.compare(i, dlen, delim) != 0) return false;
        if (style.*flag) { // closing
            flush();
            style.*flag = false;
            i += dlen;
            return true;
        }
        // opening: only if a closing delimiter exists later and the next char is not blank
        if (i + dlen >= len || text[i + dlen] == ' ') return false;
        const auto close = text.find(delim, i + dlen);
        if (close == Glib::ustring::npos) return false;
        flush();
        style.*flag = true;
        i += dlen;
        return true;
    };

    while (i < len) {
        const gunichar c = text[i];
        // [[ url | text ]] and [[ url ]]
        if (c == '[' && text.compare(i, 2, "[[") == 0) {
            const auto close = text.find("]]", i + 2);
            if (close != Glib::ustring::npos) {
                const Glib::ustring inner = text.substr(i + 2, close - i - 2);
                const auto bar = inner.find('|');
                Glib::ustring url = trim(bar == Glib::ustring::npos ? inner : inner.substr(0, bar));
                Glib::ustring label = trim(bar == Glib::ustring::npos ? inner : inner.substr(bar + 1));
                if (label.empty()) label = url;
                flush();
                Style link_style = style;
                if (is_object_reference(url)) {
                    _emit_object_reference(url, label, style);
                }
                else {
                    link_style.link = CtStrUtil::get_internal_link_from_http_url(url);
                    _emit_text(label, link_style);
                }
                i = close + 2;
                continue;
            }
        }
        // [text](url)
        if (c == '[') {
            const auto close = text.find("](", i + 1);
            if (close != Glib::ustring::npos) {
                const auto paren = text.find(')', close + 2);
                if (paren != Glib::ustring::npos) {
                    const Glib::ustring label = text.substr(i + 1, close - i - 1);
                    const Glib::ustring url = trim(text.substr(close + 2, paren - close - 2));
                    flush();
                    Style link_style = style;
                    link_style.link = CtStrUtil::get_internal_link_from_http_url(url);
                    _emit_text(label, link_style);
                    i = paren + 1;
                    continue;
                }
            }
        }
        // <http://url>
        if (c == '<' && i + 1 < len && is_url_start(text, i + 1)) {
            const auto close = text.find('>', i + 1);
            if (close != Glib::ustring::npos) {
                const Glib::ustring url = text.substr(i + 1, close - i - 1);
                flush();
                Style link_style = style;
                link_style.link = CtStrUtil::get_internal_link_from_http_url(url);
                _emit_text(url, link_style);
                i = close + 1;
                continue;
            }
        }
        // bare url
        if ((c == 'h' || c == 'f' || c == 'm') && prev_is_boundary(i) && is_url_start(text, i)) {
            auto end = i;
            while (end < len && text[end] != ' ' && text[end] != '\t' && text[end] != '<' && text[end] != '>') ++end;
            while (end > i && (text[end-1] == '.' || text[end-1] == ',' || text[end-1] == ')' || text[end-1] == ';')) --end;
            const Glib::ustring url = text.substr(i, end - i);
            flush();
            Style link_style = style;
            link_style.link = CtStrUtil::get_internal_link_from_http_url(url);
            _emit_text(url, link_style);
            i = end;
            continue;
        }
        // `mono`
        if (c == '`') {
            const auto close = text.find('`', i + 1);
            if (close != Glib::ustring::npos && close > i + 1) {
                flush();
                Style mono = style; mono.mono = true;
                _emit_text(text.substr(i + 1, close - i - 1), mono);
                i = close + 1;
                continue;
            }
        }
        // {T123} {F12} and plain object references T123 @user #project
        if (c == '{' || ((c == '@' || c == '#' || (c >= 'A' && c <= 'Z')) && prev_is_boundary(i))) {
            auto end = i;
            if (c == '{') {
                const auto close = text.find('}', i + 1);
                if (close != Glib::ustring::npos) end = close + 1;
            }
            else {
                if (c == '@' || c == '#') ++end; // the sigil itself
                while (end < len && (is_word_char(text[end]) || text[end] == '#' || text[end] == '-' || text[end] == '.')) {
                    if (text[end] == '#' && end > i && text[end-1] == '#') break;
                    ++end;
                }
                while (end > i && (text[end-1] == '.' || text[end-1] == '-')) --end;
            }
            if (end > i) {
                const Glib::ustring word = text.substr(i, end - i);
                Glib::ustring normalised;
                if (is_object_reference(word, &normalised) && !(c == '#' && text.compare(i, 2, "##") == 0)) {
                    flush();
                    _emit_object_reference(word, word, style);
                    i = end;
                    continue;
                }
            }
        }
        // //italic// (but not the // of http://)
        if (c == '/' && text.compare(i, 2, "//") == 0 && (i == 0 || text[i-1] != ':')) {
            if (style.italic) {
                flush(); style.italic = false; i += 2; continue;
            }
            if (i + 2 < len && text[i+2] != ' ') {
                // find a closing // that is not part of a url
                auto close = text.find("//", i + 2);
                while (close != Glib::ustring::npos && close > 0 && text[close-1] == ':') close = text.find("//", close + 2);
                if (close != Glib::ustring::npos) {
                    flush(); style.italic = true; i += 2; continue;
                }
            }
        }
        if (toggle("**", &Style::bold)) continue;
        if (toggle("##", &Style::mono)) continue;
        if (toggle("!!", &Style::highlight)) continue;
        if (toggle("~~", &Style::strike)) continue;
        if (toggle("__", &Style::underline)) continue;

        pending += c;
        ++i;
    }
    flush();
}

void CtRemarkupParser::_flush_paragraph()
{
    for (const Glib::ustring& line : _paragraph) {
        _emit_inline(line, Style{});
        _emit_newline();
    }
    _paragraph.clear();
}

void CtRemarkupParser::feed(const Glib::ustring& text)
{
    std::vector<Glib::ustring> lines;
    Glib::ustring::size_type start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        if (nl == Glib::ustring::npos) {
            if (start < text.size()) lines.push_back(rtrim(text.substr(start)));
            break;
        }
        lines.push_back(rtrim(text.substr(start, nl - start)));
        start = nl + 1;
    }
    try {
        _feed_lines(lines);
    }
    catch (std::exception& e) {
        spdlog::error("Exception while parsing remarkup: {}", e.what());
    }
}

void CtRemarkupParser::_feed_lines(const std::vector<Glib::ustring>& lines)
{
    const std::size_t n = lines.size();
    std::size_t i = 0;
    while (i < n) {
        const Glib::ustring& line = lines[i];
        const Glib::ustring stripped = ltrim(line);

        // blank line: paragraph break
        if (stripped.empty()) {
            _flush_paragraph();
            _emit_newline();
            ++i;
            continue;
        }

        // fenced code block, optional "lang=xxx" on the fence line or as the first line inside
        Glib::ustring fence_rest;
        if (is_fence_line(line, &fence_rest)) {
            _flush_paragraph();
            Glib::ustring language = fence_rest;
            if (language.compare(0, 5, "lang=") == 0) language = language.substr(5);
            std::vector<Glib::ustring> code;
            ++i;
            bool first = true;
            while (i < n && !is_fence_line(lines[i])) {
                if (first && (lines[i].compare(0, 5, "lang=") == 0 || lines[i].compare(0, 5, "name=") == 0
                              || lines[i].compare(0, 6, "lines=") == 0 || lines[i] == "counterexample")) {
                    // header directives: "lang=python, name=foo.py" may share one line
                    for (const Glib::ustring& directive : str::split(lines[i], ",")) {
                        const Glib::ustring d = trim(directive);
                        if (d.compare(0, 5, "lang=") == 0) language = d.substr(5);
                    }
                    ++i;
                    first = false;
                    continue;
                }
                first = false;
                code.push_back(lines[i]);
                ++i;
            }
            if (i < n) ++i; // closing fence
            _emit_codebox(language, str::join(code, "\n"));
            continue;
        }

        // literal block %%% ... %%%
        if (stripped == "%%%") {
            _flush_paragraph();
            ++i;
            while (i < n && ltrim(lines[i]) != "%%%") {
                _emit_text(lines[i], Style{});
                _emit_newline();
                ++i;
            }
            if (i < n) ++i;
            continue;
        }

        // = Header =, == Header ==, # Header (single line, not a numbered list)
        if (stripped[0] == '=' || stripped[0] == '#') {
            const gunichar marker = stripped[0];
            std::size_t level = 0;
            while (level < stripped.size() && stripped[level] == marker) ++level;
            Glib::ustring title = stripped.substr(level);
            bool is_header = level <= 6 && !title.empty() && title[0] == ' ';
            if (is_header && marker == '#') {
                // "# item" lines next to other list items are a numbered list
                ListItem probe;
                const bool prev_list = i > 0 && parse_list_item(lines[i-1], probe);
                const bool next_list = i + 1 < n && parse_list_item(lines[i+1], probe);
                if (prev_list || next_list) is_header = false;
            }
            if (is_header) {
                title = trim(title);
                // strip trailing "=" / "#"
                while (!title.empty() && title[title.size()-1] == marker) title = rtrim(title.substr(0, title.size()-1));
                _flush_paragraph();
                Style st;
                st.scale = level == 1 ? "h1" : (level == 2 ? "h2" : "h3");
                st.bold = level >= 3;
                _emit_inline(title, st);
                _emit_newline();
                ++i;
                continue;
            }
        }
        // underlined header: text line followed by ===== or -----
        if (i + 1 < n && (all_of_char(ltrim(lines[i+1]), '=', 3) || all_of_char(ltrim(lines[i+1]), '-', 3)) && !is_table_line(line)) {
            ListItem probe;
            if (!parse_list_item(line, probe)) {
                _flush_paragraph();
                Style st;
                st.scale = ltrim(lines[i+1])[0] == '=' ? "h1" : "h2";
                _emit_inline(trim(line), st);
                _emit_newline();
                i += 2;
                continue;
            }
        }
        // horizontal rule
        if (all_of_char(stripped, '-', 3) || all_of_char(stripped, '*', 3) || all_of_char(stripped, '_', 3)) {
            _flush_paragraph();
            _emit_text(_pCtConfig->hRule, Style{});
            _emit_newline();
            ++i;
            continue;
        }
        // table
        if (is_table_line(line)) {
            _flush_paragraph();
            std::vector<std::vector<Glib::ustring>> rows;
            std::size_t num_cols = 0;
            while (i < n && is_table_line(lines[i])) {
                Glib::ustring row_text = trim(lines[i]);
                if (row_text[0] == '|') row_text.erase(0, 1);
                if (!row_text.empty() && row_text[row_text.size()-1] == '|') row_text.erase(row_text.size()-1);
                std::vector<Glib::ustring> row;
                for (const Glib::ustring& cell : str::split(row_text, "|")) row.push_back(trim(cell));
                bool divider = !row.empty();
                for (const Glib::ustring& cell : row) {
                    if (cell.empty() || cell.find_first_not_of("-: ") != Glib::ustring::npos) { divider = false; break; }
                }
                if (!divider) {
                    num_cols = std::max(num_cols, row.size());
                    rows.push_back(std::move(row));
                }
                ++i;
            }
            for (auto& row : rows) while (row.size() < num_cols) row.emplace_back("");
            _emit_table(rows);
            continue;
        }
        // callouts
        {
            struct Callout { const char* label; const char* colour; };
            static const Callout callouts[] = {
                {"NOTE:", CALLOUT_NOTE_COLOUR}, {"(NOTE)", CALLOUT_NOTE_COLOUR},
                {"WARNING:", CALLOUT_WARNING_COLOUR}, {"(WARNING)", CALLOUT_WARNING_COLOUR},
                {"IMPORTANT:", CALLOUT_IMPORTANT_COLOUR}, {"(IMPORTANT)", CALLOUT_IMPORTANT_COLOUR},
            };
            bool handled = false;
            for (const Callout& co : callouts) {
                const std::size_t llen = std::strlen(co.label);
                if (stripped.compare(0, llen, co.label) == 0 && (stripped.size() == llen || stripped[llen] == ' ')) {
                    _flush_paragraph();
                    Style label_style;
                    label_style.bold = true;
                    label_style.foreground = co.colour;
                    Glib::ustring label = co.label;
                    if (label[0] == '(') label = label.substr(1, label.size() - 2) + ":";
                    _emit_text(label + " ", label_style);
                    _emit_inline(ltrim(stripped.substr(llen)), Style{});
                    _emit_newline();
                    ++i;
                    handled = true;
                    break;
                }
            }
            if (handled) continue;
        }
        // quote
        if (stripped[0] == '>') {
            _flush_paragraph();
            while (i < n && !ltrim(lines[i]).empty() && ltrim(lines[i])[0] == '>') {
                Glib::ustring q = ltrim(lines[i]).substr(1);
                if (!q.empty() && q[0] == ' ') q = q.substr(1);
                Style bar; bar.foreground = QUOTE_COLOUR;
                _emit_text("▏ ", bar);
                Style st; st.italic = true;
                _emit_inline(q, st);
                _emit_newline();
                ++i;
            }
            continue;
        }
        // lists
        ListItem item;
        if (parse_list_item(line, item)) {
            _flush_paragraph();
            std::vector<int> counters;
            std::vector<bool> ordered_at_level;
            while (i < n && parse_list_item(lines[i], item)) {
                if (static_cast<int>(counters.size()) <= item.level) {
                    counters.resize(item.level + 1, 0);
                    ordered_at_level.resize(item.level + 1, item.ordered);
                }
                else {
                    counters.resize(item.level + 1);
                    ordered_at_level.resize(item.level + 1);
                }
                if (ordered_at_level[item.level] != item.ordered) {
                    // switching between bullets and numbers restarts the numbering
                    counters[item.level] = 0;
                    ordered_at_level[item.level] = item.ordered;
                }
                ++counters[item.level];
                Glib::ustring prefix(static_cast<Glib::ustring::size_type>(item.level) * 2, ' ');
                if (item.checkbox >= 0) {
                    const auto& todo = _pCtConfig->charsTodo;
                    prefix += (item.checkbox == 1 && todo.size() > 1) ? todo[1] : todo[0];
                    prefix += " ";
                }
                else if (item.ordered) {
                    prefix += std::to_string(counters[item.level]) + ". ";
                }
                else {
                    const auto& bullets = _pCtConfig->charsListbul;
                    const std::size_t idx = std::min<std::size_t>(item.level, bullets.size() - 1);
                    prefix += bullets[idx];
                    prefix += " ";
                }
                _emit_text(prefix, Style{});
                Style st;
                if (item.checkbox == 1) st.strike = true;
                _emit_inline(item.text, st);
                _emit_newline();
                ++i;
            }
            continue;
        }
        // indented (two spaces) code block
        if (leading_spaces(line) >= 2) {
            _flush_paragraph();
            std::vector<Glib::ustring> code;
            while (i < n && (leading_spaces(lines[i]) >= 2 || lines[i].empty())) {
                if (lines[i].empty() && (i + 1 >= n || leading_spaces(lines[i+1]) < 2)) break;
                code.push_back(lines[i].size() >= 2 ? lines[i].substr(2) : Glib::ustring{});
                ++i;
            }
            _emit_codebox("", str::join(code, "\n"));
            continue;
        }
        // plain paragraph line
        _paragraph.push_back(line);
        ++i;
    }
    _flush_paragraph();
}
