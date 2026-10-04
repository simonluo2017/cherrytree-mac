/*
 * ct_parser_remarkup.h
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

#pragma once

#include "ct_parser.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class CtConfig;

/**
 * @brief Remarkup parser (https://we.phorge.it/book/phorge/article/remarkup/)
 *
 * Line based: block elements (headers, lists, code blocks, tables, callouts,
 * quotes, rules) are recognised per line, inline formatting is applied to
 * the text of each block. The output is the same <root><slot>...</slot></root>
 * XML that CtDocumentBuilder produces, so it can be loaded with
 * CtStorageXmlHelper::get_text_buffer_one_slot_from_xml.
 *
 * Object references (T123, D45, {F12}, @user, #project, ...) are resolved
 * through an optional callback to a node of the current document; when the
 * callback returns a node id the reference becomes a link to that node,
 * otherwise it is shown as grey monospace text.
 */
class CtRemarkupParser : public CtParserInterface
{
public:
    /// returns the id of the node a reference points to, or nullopt when unknown
    using RefResolver = std::function<std::optional<gint64>(const Glib::ustring& reference)>;

    explicit CtRemarkupParser(const CtConfig* pCtConfig);
    ~CtRemarkupParser() override;

    void set_ref_resolver(RefResolver resolver) { _refResolver = std::move(resolver); }

    void wipe();
    void feed(const Glib::ustring& text) override;

    xmlpp::Node* root_node() const;
    std::string to_string() const;

    /// Object reference pattern used by both the parser and the resolver
    /// (T123, D45, P6, M7, F8, V9, Z10 with optional {} and #anchor, @user, #project)
    static bool is_object_reference(const Glib::ustring& word, Glib::ustring* pNormalised = nullptr);

private:
    struct Style {
        bool bold{false};
        bool italic{false};
        bool mono{false};
        bool strike{false};
        bool underline{false};
        bool highlight{false};
        std::string link;       // "webs http://..." or "node 12"
        std::string foreground; // "#rrggbb"
        std::string scale;      // "h1".."h3", "small"
    };

    void _emit_text(const Glib::ustring& text, const Style& style);
    void _emit_newline();
    void _emit_inline(const Glib::ustring& text, Style style);
    void _emit_codebox(const Glib::ustring& language, const Glib::ustring& code);
    void _emit_table(const std::vector<std::vector<Glib::ustring>>& rows);
    void _emit_object_reference(const Glib::ustring& reference, const Glib::ustring& display, Style style);

    void _flush_paragraph();
    void _feed_lines(const std::vector<Glib::ustring>& lines);

    const CtConfig* const _pCtConfig;
    RefResolver _refResolver;

    std::unique_ptr<xmlpp::Document> _pDocument;
    xmlpp::Element* _pSlot{nullptr};
    int _charOffset{0};

    std::vector<Glib::ustring> _paragraph; // pending paragraph lines (joined by newline)
};
