/*
 * ct_search_index.h
 *
 * Full text search index of a CherryTree document (SQLite FTS5), kept in a
 * separate, disposable file next to the document: <name>.ai-index.sqlite
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

#include "ct_filesystem.h"

#include <glibmm/ustring.h>
#include <map>
#include <string>
#include <vector>

struct sqlite3;

struct CtChunk
{
    gint64        chunk_id{0};
    gint64        node_id{0};
    int           seq{0};
    int           start_offset{0};
    Glib::ustring text;
};

struct CtSemanticResult
{
    gint64        chunk_id{0};
    gint64        node_id{0};
    Glib::ustring text;
    double        distance{0.0}; // cosine distance, lower is closer
};

struct CtSearchResult
{
    gint64        node_id{0};
    Glib::ustring node_name;
    Glib::ustring node_path;   // "Parent / Child / Node"
    Glib::ustring snippet;     // plain text excerpt around the first match
    double        rank{0.0};   // bm25, lower is better
};

/**
 * @brief SQLite FTS5 index of node names, tags and plain text content.
 *
 * The index is derived data: it can be deleted at any time and is rebuilt
 * from the document. CJK text is indexed one character per token so that
 * any substring of a Chinese/Japanese/Korean sentence can be searched.
 */
class CtSearchIndex
{
public:
    static constexpr int SCHEMA_VERSION{1};
    static const char* INDEX_FILE_SUFFIX; // ".ai-index.sqlite"

    CtSearchIndex() = default;
    ~CtSearchIndex();
    CtSearchIndex(const CtSearchIndex&) = delete;
    CtSearchIndex& operator=(const CtSearchIndex&) = delete;

    /// the index file that belongs to a document
    static fs::path index_path_for_document(const fs::path& document_path);

    bool open(const fs::path& index_path, std::string* pError = nullptr);
    void close();
    bool is_open() const { return nullptr != _pDb; }
    const fs::path& get_path() const { return _path; }

    bool index_node(const gint64 node_id,
                    const Glib::ustring& name,
                    const Glib::ustring& path,
                    const Glib::ustring& tags,
                    const Glib::ustring& body,
                    const gint64 mtime);
    bool remove_node(const gint64 node_id);
    bool clear();
    /// node id -> modification time stored with the indexed content
    std::map<gint64, gint64> indexed_nodes() const;
    gint64 count_nodes() const;

    /// bm25 ranked search; the whole query must match (AND of the terms, prefix match for latin words)
    std::vector<CtSearchResult> search(const Glib::ustring& user_query, const int limit) const;

    // ---- semantic index (chunks + sqlite-vec vectors), see docs
    /// set the embedding model; a different model/dimension than the stored one drops the vectors
    bool semantic_configure(const std::string& model_name, const int dim);
    std::string semantic_model_name() const;
    int semantic_dim() const;
    /// split a node text into overlapping chunks (the node name is prefixed to each)
    static std::vector<Glib::ustring> make_chunks(const Glib::ustring& node_name, const Glib::ustring& body,
                                                  const size_t target_chars = 1000, const size_t overlap_chars = 200);
    /// replace the chunks of a node; chunks whose text is unchanged keep their vector
    bool replace_node_chunks(const gint64 node_id, const std::vector<Glib::ustring>& chunks);
    bool remove_node_chunks(const gint64 node_id);
    /// chunks that still need a vector
    std::vector<CtChunk> pending_chunks(const int limit) const;
    /// all chunks of a node in order
    std::vector<CtChunk> chunks_of_node(const gint64 node_id) const;
    gint64 count_pending_chunks() const;
    gint64 count_embedded_chunks() const;
    bool store_embedding(const gint64 chunk_id, const std::vector<float>& vec);
    /// nearest chunks to a query vector
    std::vector<CtSemanticResult> semantic_search(const std::vector<float>& query, const int limit) const;
    /// mean vector of a node's chunks (empty when none embedded)
    std::vector<float> node_vector(const gint64 node_id) const;
    /// register the sqlite-vec extension for every connection (idempotent)
    static void register_vec_extension();

    // helpers, public for the tests
    static std::vector<Glib::ustring> query_terms(const Glib::ustring& user_query);
    static Glib::ustring build_fts_query(const Glib::ustring& user_query);
    /// insert spaces around CJK characters so that the unicode61 tokenizer indexes them one by one
    static Glib::ustring cjk_space(const Glib::ustring& text);
    static bool is_cjk(const gunichar ch);
    static Glib::ustring make_snippet(const Glib::ustring& body, const std::vector<Glib::ustring>& terms, const int window_chars);

private:
    bool _exec(const char* sql, std::string* pError = nullptr);
    bool _set_meta(const std::string& key, const std::string& value);
    std::string _get_meta(const std::string& key) const;
    bool _ensure_vec_table(const int dim);

    sqlite3* _pDb{nullptr};
    fs::path _path;
};
