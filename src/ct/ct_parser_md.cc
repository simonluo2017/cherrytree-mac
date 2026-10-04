/*
 * ct_parser_md.cc
 *
 * Copyright 2009-2022
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
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

#include "ct_parser.h"
#include "ct_const.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"
#include "ct_config.h"

CtMDParser::CtMDParser(CtConfig* config) : CtDocBuildingParser{config}, _text_parser{std::make_unique<CtTextParser>(_token_schemas())} {}

void CtMDParser::_add_scale_to_last(int level) {
    doc_builder().with_last_element([this, level]{ doc_builder().add_scale_tag(level, std::nullopt); });
}

std::vector<CtTextParser::token_schema> CtMDParser::_token_schemas()
{
    auto add_codebox = [this](const std::string& data) {
        auto data_iter = data.begin();
        while (data_iter != data.end() && *data_iter != '\n') {
            ++data_iter;
        }
        std::string text(data_iter, data.end());
        std::string lang;
        if (data_iter != data.begin()) {
            lang.append(data.begin(), data_iter);
        }
        spdlog::debug("CODEBOX: {}, lang: {}", text, lang);
        doc_builder().add_codebox(lang, text);

    };
    auto add_h3 = [this](const std::string& text) {
        doc_builder().close_current_tag();
        doc_builder().add_scale_tag(3, text + "\n");
        doc_builder().close_current_tag();
    };
    auto add_list = [this](const std::string& text) {
        doc_builder().add_list(_list_level, "");
        feed(text);
        doc_builder().add_newline();
        _list_level = 0;
    };

    return {
        // Bold
        {"__", true,  true,  [this](const std::string &data) {
            doc_builder().add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, data);
        }},
        // Italic
        {"_", true, true, [this](const std::string& data){
            doc_builder().add_italic_tag(data);
        }},
        // Italic
        {"*", true, true, [this](const std::string& data){
            doc_builder().add_italic_tag(data);
        }},
        // Bold and italic
        {"***", true, true, [this](const std::string& data){
            doc_builder().add_italic_tag(data);
            doc_builder().add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, std::nullopt);
        }},
        // Bold
        {"**", true,  true,  [this](const std::string &data) {
            doc_builder().add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, data);
        }},
        // First part of a link
        {"[",  true,  false, [this](const std::string &data) {
            // Parse for end of display
            auto last_pos = data.find_last_of(']');
            if (last_pos == std::string::npos) {
                spdlog::warn("Unknown data captured: {}; printing as plaintext", data);
                doc_builder().add_text(data);
                return;
            }

            std::string title(data.begin(), data.begin() + last_pos);
            std::string url(data.begin() + last_pos + 2, data.end());

            doc_builder().add_text(title, false);
            doc_builder().add_link(url);
            doc_builder().close_current_tag(); // text following the link must not inherit it
        }, ")", true},
        // Monospace
        {"`", true, true, [this](const std::string& data){
            doc_builder().add_monospace_tag(data);
        }, "`", true},
        // Footnote
        {"[^", true, false, [this](const std::string& data){
            // Todo: Implement footnotes
            doc_builder().add_text("[^" + data + "]");
        }, "]"},
        // Codebox(s)
        {"~~~", true, true, add_codebox, "~~~", true},
        {"```", true, true, add_codebox, "```", true},
        // List
        {"* ", true, false, add_list, "\n", true},
        // Also list
        {"- ", true, false, add_list, "\n", true},

        // Passthroughs for lists
        {" -", true, false, [](const std::string&){}, " "},
        {"-", true, true, [](const std::string&){}},
        // Strikethrough
        {"~~", true,  true,  [this](const std::string &data) {
            doc_builder().add_strikethrough_tag(data);
        }},
        // Passthrough for ``` and `
        {"``", true, true, [this](const std::string& data){ doc_builder().add_text("``" + data + "``"); }},
        // Headers (h1, h2, etc)
        {"# ",  true, false, [this](const std::string &data) {
            doc_builder().close_current_tag();
            doc_builder().add_scale_tag(1, data + "\n");
            doc_builder().close_current_tag();
        }, "\n"},
        {"## ",  true, false, [this](const std::string &data) {
            doc_builder().close_current_tag();
            doc_builder().add_scale_tag(2, data + "\n");
            doc_builder().close_current_tag();
        }, "\n"},
        {"### ",  true, false,  add_h3, "\n"},
        {"#### ",  true, false,  add_h3, "\n"},
        {"##### ",  true, false,  add_h3, "\n"},
        {"###### ",  true, false,  add_h3, "\n"},

        // H1
        {"\n==", true, false, [this](const std::string&){
            _add_scale_to_last(1);
            doc_builder().add_newline();
        }, "\n"},
        // H2
        {"\n----", true, false, [this](const std::string&){
            _add_scale_to_last(2);
            doc_builder().add_newline();
        }, "\n"},
        // Horizontal divider
        {"***\n", true, false, [this](const std::string&){
            doc_builder().add_hrule();
        }, " "},
        // Tables are handled line by line in CtMDParser::feed, not here:
        // the token based approach broke on padded cells and on text following a table

        // Image link
        {"![", true, false, [this](const std::string& data){
            auto last_pos = data.find_last_of(']');
            if (last_pos == std::string::npos) {
                spdlog::warn("Image captured unknown data: <{}>; printing as plaintext", data);
                doc_builder().add_text(data);
                return;
            }

            std::string title(data.begin(), data.begin() + last_pos);
            std::string uri(data.begin() + last_pos + 2, data.end());

            doc_builder().add_text(title, false);
            doc_builder().add_image(uri);
        }, ")", true},
        // Link
        {"<", true, false, [this](const std::string& data){
            doc_builder().add_text(data, false);
            doc_builder().add_link(data);
        }, ">", true}
    };
}

void CtMDParser::_place_free_text()
{
    if (!_free_text.empty()) {
        auto iter = _free_text.rbegin();
        for (; iter != _free_text.rend(); ++iter) {
            if (*iter == '\n') break;
        }
        Glib::ustring last_line{iter.base(), _free_text.end()};
        Glib::ustring other_txt{_free_text.begin(), iter.base()};
        doc_builder().add_text(other_txt);
        doc_builder().add_text(last_line); // This may be needed for headers

        _free_text.clear();
    }
}

void CtMDParser::feed(const Glib::ustring& buffer)
{
    // Line based pre-pass: table blocks (consecutive lines starting with '|') are
    // extracted and built directly, everything else goes through the tokenizer.
    // Lines inside fenced code blocks are never treated as tables.
    std::vector<Glib::ustring> lines;
    {
        Glib::ustring::size_type start = 0;
        while (start <= buffer.size()) {
            const auto nl = buffer.find('\n', start);
            if (nl == Glib::ustring::npos) {
                if (start < buffer.size()) lines.push_back(buffer.substr(start));
                break;
            }
            lines.push_back(buffer.substr(start, nl - start + 1)); // keep the newline
            start = nl + 1;
        }
    }
    auto is_table_line = [](const Glib::ustring& line) {
        const auto first = line.find_first_not_of(" \t");
        return first != Glib::ustring::npos && line[first] == '|';
    };
    auto is_fence_line = [](const Glib::ustring& line) {
        const auto first = line.find_first_not_of(" \t");
        if (first == Glib::ustring::npos) return false;
        const Glib::ustring rest = line.substr(first, 3);
        return rest == "```" || rest == "~~~";
    };
    Glib::ustring pending;
    bool in_fence = false;
    for (std::size_t i = 0; i < lines.size();) {
        if (!in_fence && is_table_line(lines[i])) {
            std::vector<Glib::ustring> table_lines;
            while (i < lines.size() && is_table_line(lines[i])) {
                table_lines.push_back(lines[i]);
                ++i;
            }
            if (!pending.empty()) { _feed_tokens(pending); pending.clear(); }
            _feed_table_block(table_lines);
            continue;
        }
        if (is_fence_line(lines[i])) in_fence = !in_fence;
        pending += lines[i];
        ++i;
    }
    if (!pending.empty()) _feed_tokens(pending);
}

void CtMDParser::_feed_table_block(const std::vector<Glib::ustring>& lines)
{
    TableMatrix matrix;
    std::size_t num_cols = 0;
    for (const Glib::ustring& raw_line : lines) {
        Glib::ustring line = str::trim(raw_line);
        if (line.empty()) continue;
        if (line[0] == '|') line.erase(0, 1);
        if (!line.empty() && line[line.size() - 1] == '|') line.erase(line.size() - 1);
        TableRow row;
        for (const Glib::ustring& cell : str::split(line, "|")) {
            row.push_back(str::trim(cell));
        }
        // header divider: every cell made of '-' and ':' only
        bool is_divider = !row.empty();
        for (const Glib::ustring& cell : row) {
            if (cell.empty() || cell.find_first_not_of("-: ") != Glib::ustring::npos) { is_divider = false; break; }
        }
        if (is_divider) continue;
        num_cols = std::max(num_cols, row.size());
        matrix.push_back(std::move(row));
    }
    if (matrix.empty()) return;
    for (TableRow& row : matrix) {
        while (row.size() < num_cols) row.emplace_back("");
    }
    _place_free_text();
    doc_builder().close_current_tag();
    doc_builder().add_table(matrix);
    doc_builder().add_newline();
}

void CtMDParser::_feed_tokens(const Glib::ustring& buffer)
{
    try {
        auto tokens_raw = _text_parser->tokenize(buffer);
        auto tokens     = _text_parser->parse_tokens(tokens_raw);

        for (auto iter = tokens.begin(); iter != tokens.end(); ++iter) {
            if (iter->first) {
                _place_free_text();
                // This is needed for links with () in them
                if ((iter + 1) != tokens.end()) {
                    if (!(iter + 1)->first && ((iter + 1)->second == ")")) {
                        // Excess bracket from link
                        iter->first->action(iter->second + ")");
                        ++iter;
                        if ((iter + 1) != tokens.end()) ++iter;

                        continue;
                    }
                }
                iter->first->action(iter->second);
            }
            else {
                if (!iter->second.empty()) {
                    _free_text += iter->second;
                }
            }
            _last_encountered_token = iter->first;
        }
        _place_free_text();
    }
    catch (std::exception& e) {
        spdlog::error("Exception while parsing '{}': {}", buffer.c_str(), e.what());
    }
}

void CtMDParser::_add_table_cell(const std::string& text)
{
    if (!text.empty()) {
        // Parse it to see if a cell or end of row
        char last_ch = text.back();
        if (last_ch == '|') {
            // End of row
            _current_table_row.emplace_back(text.begin(), text.end() - 1);
            _pop_table_row();
        } else {
            // Just a cell
            _current_table_row.emplace_back(text);
        }
    } else {
        if (not _current_table_row.empty()) { _pop_table_row(); }
        //spdlog::warn("_add_table_cell called without text, the document may contain invalid or unknown formatting");
    }
}

void CtMDParser::_pop_table()
{
    doc_builder().add_table(_current_table);
    _current_table.clear();
}

void CtMDParser::_pop_table_row()
{
    _current_table.emplace_back(_current_table_row);
    _current_table_row.clear();
}
