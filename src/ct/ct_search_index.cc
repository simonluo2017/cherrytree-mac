/*
 * ct_search_index.cc
 *
 * Full text search index of a CherryTree document (SQLite FTS5).
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

#include "ct_search_index.h"
#include "ct_logging.h"

#include <sqlite3.h>
// sqlite-vec.h pulls in sqlite3ext.h, which turns the whole sqlite3 API into macros over
// sqlite3_api: declare the one entry point we need instead (defined in third_party/sqlite-vec)
struct sqlite3_api_routines;
extern "C" int sqlite3_vec_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi);
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

const char* CtSearchIndex::INDEX_FILE_SUFFIX{".ai-index.sqlite"};

namespace {

// RAII prepared statement
class Stmt
{
public:
    Stmt(sqlite3* pDb, const char* sql) {
        if (sqlite3_prepare_v2(pDb, sql, -1, &_pStmt, nullptr) != SQLITE_OK) {
            spdlog::error("search index: sqlite3_prepare_v2 '{}': {}", sql, sqlite3_errmsg(pDb));
            _pStmt = nullptr;
        }
    }
    ~Stmt() { if (_pStmt) sqlite3_finalize(_pStmt); }
    operator bool() const { return nullptr != _pStmt; }
    sqlite3_stmt* get() { return _pStmt; }
    void bind_text(const int idx, const Glib::ustring& text) {
        sqlite3_bind_text(_pStmt, idx, text.c_str(), static_cast<int>(text.bytes()), SQLITE_TRANSIENT);
    }
private:
    sqlite3_stmt* _pStmt{nullptr};
};

Glib::ustring column_text(sqlite3_stmt* pStmt, const int col)
{
    const unsigned char* pText = sqlite3_column_text(pStmt, col);
    return pText ? Glib::ustring{reinterpret_cast<const char*>(pText)} : Glib::ustring{};
}

} // namespace

CtSearchIndex::~CtSearchIndex()
{
    close();
}

/*static*/ fs::path CtSearchIndex::index_path_for_document(const fs::path& document_path)
{
    if (document_path.empty()) return fs::path{};
    fs::path index_path = document_path.parent_path() / document_path.stem();
    index_path += INDEX_FILE_SUFFIX;
    return index_path;
}

bool CtSearchIndex::_exec(const char* sql, std::string* pError)
{
    char* pErrMsg{nullptr};
    if (sqlite3_exec(_pDb, sql, nullptr, nullptr, &pErrMsg) != SQLITE_OK) {
        const std::string err = pErrMsg ? pErrMsg : "unknown error";
        sqlite3_free(pErrMsg);
        spdlog::error("search index: '{}': {}", sql, err);
        if (pError) *pError = err;
        return false;
    }
    return true;
}

/*static*/ void CtSearchIndex::register_vec_extension()
{
    static bool registered{false};
    if (registered) return;
    registered = true;
    sqlite3_auto_extension(reinterpret_cast<void(*)(void)>(sqlite3_vec_init));
}

bool CtSearchIndex::open(const fs::path& index_path, std::string* pError)
{
    close();
    register_vec_extension();
    if (sqlite3_open(index_path.c_str(), &_pDb) != SQLITE_OK) {
        if (pError) *pError = _pDb ? sqlite3_errmsg(_pDb) : "sqlite3_open failed";
        close();
        return false;
    }
    _path = index_path;
    if (not _exec("PRAGMA journal_mode=WAL", pError) or not _exec("PRAGMA synchronous=NORMAL", pError)) {
        close();
        return false;
    }
    // schema version check: a different version means rebuild from scratch
    bool rebuild{false};
    {
        Stmt stmt{_pDb, "SELECT name FROM sqlite_master WHERE type='table' AND name='meta'"};
        if (stmt and sqlite3_step(stmt.get()) == SQLITE_ROW) {
            Stmt stmtVer{_pDb, "SELECT value FROM meta WHERE key='schema_version'"};
            if (stmtVer and sqlite3_step(stmtVer.get()) == SQLITE_ROW) {
                rebuild = sqlite3_column_int(stmtVer.get(), 0) != SCHEMA_VERSION;
            }
            else {
                rebuild = true;
            }
        }
    }
    if (rebuild) {
        _exec("DROP TABLE IF EXISTS nodes_fts");
        _exec("DROP TABLE IF EXISTS meta");
    }
    const char* schema =
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
        "CREATE VIRTUAL TABLE IF NOT EXISTS nodes_fts USING fts5("
        "  node_id UNINDEXED, path UNINDEXED, mtime UNINDEXED, name_raw UNINDEXED, body_raw UNINDEXED,"
        "  name, tags, body,"
        "  tokenize='unicode61 remove_diacritics 2');"
        "INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '1');"
        "CREATE TABLE IF NOT EXISTS chunks("
        "  chunk_id INTEGER PRIMARY KEY, node_id INTEGER NOT NULL, seq INTEGER NOT NULL,"
        "  start_offset INTEGER NOT NULL DEFAULT 0, text TEXT NOT NULL, text_hash TEXT NOT NULL,"
        "  embedded INTEGER NOT NULL DEFAULT 0);"
        "CREATE INDEX IF NOT EXISTS chunks_node ON chunks(node_id);"
        "CREATE INDEX IF NOT EXISTS chunks_embedded ON chunks(embedded);";
    if (not _exec(schema, pError)) {
        close();
        return false;
    }
    return true;
}

bool CtSearchIndex::_set_meta(const std::string& key, const std::string& value)
{
    Stmt stmt{_pDb, "INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)"};
    if (not stmt) return false;
    stmt.bind_text(1, key);
    stmt.bind_text(2, value);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

std::string CtSearchIndex::_get_meta(const std::string& key) const
{
    Stmt stmt{_pDb, "SELECT value FROM meta WHERE key=?"};
    if (not stmt) return {};
    stmt.bind_text(1, key);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) return column_text(stmt.get(), 0).raw();
    return {};
}

std::string CtSearchIndex::semantic_model_name() const
{
    return _pDb ? _get_meta("embedding_model") : std::string{};
}

int CtSearchIndex::semantic_dim() const
{
    if (not _pDb) return 0;
    const std::string dim = _get_meta("embedding_dim");
    return dim.empty() ? 0 : std::atoi(dim.c_str());
}

bool CtSearchIndex::_ensure_vec_table(const int dim)
{
    const std::string sql = "CREATE VIRTUAL TABLE IF NOT EXISTS vec_chunks USING vec0(chunk_id INTEGER PRIMARY KEY, embedding float[" + std::to_string(dim) + "] distance_metric=cosine)";
    return _exec(sql.c_str());
}

bool CtSearchIndex::semantic_configure(const std::string& model_name, const int dim)
{
    if (not _pDb or dim <= 0) return false;
    const std::string stored_model = _get_meta("embedding_model");
    const int stored_dim = semantic_dim();
    if (stored_model != model_name or stored_dim != dim) {
        // a different vector space: every chunk has to be embedded again
        _exec("DROP TABLE IF EXISTS vec_chunks");
        _exec("UPDATE chunks SET embedded=0");
        _set_meta("embedding_model", model_name);
        _set_meta("embedding_dim", std::to_string(dim));
        spdlog::info("search index: semantic index reset for model {} ({} dims)", model_name, dim);
    }
    return _ensure_vec_table(dim);
}

/*static*/ std::vector<Glib::ustring> CtSearchIndex::make_chunks(const Glib::ustring& node_name, const Glib::ustring& body,
                                                                 const size_t target_chars, const size_t overlap_chars)
{
    std::vector<Glib::ustring> chunks;
    // paragraphs separated by blank lines; long paragraphs are cut at line ends / hard limit
    std::vector<Glib::ustring> paragraphs;
    {
        Glib::ustring current;
        Glib::ustring::size_type start = 0;
        while (start <= body.size()) {
            const auto nl = body.find('\n', start);
            const Glib::ustring line = nl == Glib::ustring::npos ? body.substr(start) : body.substr(start, nl - start);
            const bool blank = line.find_first_not_of(" \t\r") == Glib::ustring::npos;
            if (blank) {
                if (not current.empty()) { paragraphs.push_back(current); current.clear(); }
            }
            else {
                if (not current.empty()) current += "\n";
                current += line;
                while (current.size() > target_chars * 2) { // a huge paragraph
                    paragraphs.push_back(current.substr(0, target_chars));
                    current = current.substr(target_chars);
                }
            }
            if (nl == Glib::ustring::npos) break;
            start = nl + 1;
        }
        if (not current.empty()) paragraphs.push_back(current);
    }
    Glib::ustring chunk;
    for (const Glib::ustring& para : paragraphs) {
        if (not chunk.empty() and chunk.size() + para.size() + 1 > target_chars) {
            chunks.push_back(chunk);
            // overlap: carry the tail of the previous chunk
            chunk = chunk.size() > overlap_chars ? chunk.substr(chunk.size() - overlap_chars) : chunk;
            chunk += "\n";
        }
        if (not chunk.empty() and chunk[chunk.size()-1] != '\n') chunk += "\n";
        chunk += para;
    }
    if (chunk.find_first_not_of(" \t\r\n") != Glib::ustring::npos) chunks.push_back(chunk);
    if (chunks.empty() and not node_name.empty()) chunks.push_back(Glib::ustring{});
    for (Glib::ustring& c : chunks) c = node_name + "\n" + c;
    return chunks;
}

bool CtSearchIndex::replace_node_chunks(const gint64 node_id, const std::vector<Glib::ustring>& chunks)
{
    if (not _pDb) return false;
    // existing chunks by hash (identical chunks can repeat): unchanged text keeps its id and vector
    std::map<std::string, std::vector<gint64>> existing;
    std::vector<gint64> all_existing;
    {
        Stmt stmt{_pDb, "SELECT chunk_id, text_hash FROM chunks WHERE node_id=?"};
        if (not stmt) return false;
        sqlite3_bind_int64(stmt.get(), 1, node_id);
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            const gint64 id = sqlite3_column_int64(stmt.get(), 0);
            existing[column_text(stmt.get(), 1).raw()].push_back(id);
            all_existing.push_back(id);
        }
    }
    std::set<gint64> keep;
    _exec("BEGIN");
    int seq{0};
    for (const Glib::ustring& text : chunks) {
        gchar* pHash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, text.c_str(), text.bytes());
        const std::string hash = pHash ? pHash : "";
        g_free(pHash);
        auto found = existing.find(hash);
        if (found != existing.end() and not found->second.empty()) {
            const gint64 id = found->second.back();
            found->second.pop_back();
            keep.insert(id);
            Stmt stmt{_pDb, "UPDATE chunks SET seq=? WHERE chunk_id=?"};
            if (stmt) { sqlite3_bind_int(stmt.get(), 1, seq); sqlite3_bind_int64(stmt.get(), 2, id); sqlite3_step(stmt.get()); }
        }
        else {
            Stmt stmt{_pDb, "INSERT INTO chunks(node_id, seq, start_offset, text, text_hash, embedded) VALUES(?,?,0,?,?,0)"};
            if (not stmt) { _exec("ROLLBACK"); return false; }
            sqlite3_bind_int64(stmt.get(), 1, node_id);
            sqlite3_bind_int(stmt.get(), 2, seq);
            stmt.bind_text(3, text);
            stmt.bind_text(4, hash);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) { _exec("ROLLBACK"); return false; }
            keep.insert(sqlite3_last_insert_rowid(_pDb));
        }
        ++seq;
    }
    // drop the chunks that are gone (and their vectors)
    const bool has_vec = semantic_dim() > 0;
    for (const gint64 chunk_id : all_existing) {
        if (keep.count(chunk_id)) continue;
        Stmt del{_pDb, "DELETE FROM chunks WHERE chunk_id=?"};
        if (del) { sqlite3_bind_int64(del.get(), 1, chunk_id); sqlite3_step(del.get()); }
        if (has_vec) {
            Stmt delv{_pDb, "DELETE FROM vec_chunks WHERE chunk_id=?"};
            if (delv) { sqlite3_bind_int64(delv.get(), 1, chunk_id); sqlite3_step(delv.get()); }
        }
    }
    _exec("COMMIT");
    return true;
}

bool CtSearchIndex::remove_node_chunks(const gint64 node_id)
{
    if (not _pDb) return false;
    if (semantic_dim() > 0) {
        Stmt stmt{_pDb, "DELETE FROM vec_chunks WHERE chunk_id IN (SELECT chunk_id FROM chunks WHERE node_id=?)"};
        if (stmt) { sqlite3_bind_int64(stmt.get(), 1, node_id); sqlite3_step(stmt.get()); }
    }
    Stmt stmt{_pDb, "DELETE FROM chunks WHERE node_id=?"};
    if (not stmt) return false;
    sqlite3_bind_int64(stmt.get(), 1, node_id);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

std::vector<CtChunk> CtSearchIndex::pending_chunks(const int limit) const
{
    std::vector<CtChunk> ret;
    if (not _pDb) return ret;
    Stmt stmt{_pDb, "SELECT chunk_id, node_id, seq, start_offset, text FROM chunks WHERE embedded=0 ORDER BY chunk_id LIMIT ?"};
    if (not stmt) return ret;
    sqlite3_bind_int(stmt.get(), 1, limit);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        CtChunk c;
        c.chunk_id = sqlite3_column_int64(stmt.get(), 0);
        c.node_id = sqlite3_column_int64(stmt.get(), 1);
        c.seq = sqlite3_column_int(stmt.get(), 2);
        c.start_offset = sqlite3_column_int(stmt.get(), 3);
        c.text = column_text(stmt.get(), 4);
        ret.push_back(std::move(c));
    }
    return ret;
}

std::vector<CtChunk> CtSearchIndex::chunks_of_node(const gint64 node_id) const
{
    std::vector<CtChunk> ret;
    if (not _pDb) return ret;
    Stmt stmt{_pDb, "SELECT chunk_id, node_id, seq, start_offset, text FROM chunks WHERE node_id=? ORDER BY seq"};
    if (not stmt) return ret;
    sqlite3_bind_int64(stmt.get(), 1, node_id);
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        CtChunk c;
        c.chunk_id = sqlite3_column_int64(stmt.get(), 0);
        c.node_id = sqlite3_column_int64(stmt.get(), 1);
        c.seq = sqlite3_column_int(stmt.get(), 2);
        c.start_offset = sqlite3_column_int(stmt.get(), 3);
        c.text = column_text(stmt.get(), 4);
        ret.push_back(std::move(c));
    }
    return ret;
}

gint64 CtSearchIndex::count_pending_chunks() const
{
    if (not _pDb) return 0;
    Stmt stmt{_pDb, "SELECT count(*) FROM chunks WHERE embedded=0"};
    if (stmt and sqlite3_step(stmt.get()) == SQLITE_ROW) return sqlite3_column_int64(stmt.get(), 0);
    return 0;
}

gint64 CtSearchIndex::count_embedded_chunks() const
{
    if (not _pDb) return 0;
    Stmt stmt{_pDb, "SELECT count(*) FROM chunks WHERE embedded=1"};
    if (stmt and sqlite3_step(stmt.get()) == SQLITE_ROW) return sqlite3_column_int64(stmt.get(), 0);
    return 0;
}

bool CtSearchIndex::store_embedding(const gint64 chunk_id, const std::vector<float>& vec)
{
    if (not _pDb or vec.empty()) return false;
    {
        Stmt del{_pDb, "DELETE FROM vec_chunks WHERE chunk_id=?"};
        if (del) { sqlite3_bind_int64(del.get(), 1, chunk_id); sqlite3_step(del.get()); }
    }
    Stmt stmt{_pDb, "INSERT INTO vec_chunks(chunk_id, embedding) VALUES(?, ?)"};
    if (not stmt) return false;
    sqlite3_bind_int64(stmt.get(), 1, chunk_id);
    sqlite3_bind_blob(stmt.get(), 2, vec.data(), static_cast<int>(vec.size() * sizeof(float)), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        spdlog::warn("search index: store embedding {}: {}", chunk_id, sqlite3_errmsg(_pDb));
        return false;
    }
    Stmt upd{_pDb, "UPDATE chunks SET embedded=1 WHERE chunk_id=?"};
    if (upd) { sqlite3_bind_int64(upd.get(), 1, chunk_id); sqlite3_step(upd.get()); }
    return true;
}

std::vector<CtSemanticResult> CtSearchIndex::semantic_search(const std::vector<float>& query, const int limit) const
{
    std::vector<CtSemanticResult> ret;
    if (not _pDb or query.empty()) return ret;
    Stmt stmt{_pDb,
        "SELECT v.chunk_id, c.node_id, c.text, v.distance FROM vec_chunks v JOIN chunks c ON c.chunk_id = v.chunk_id"
        " WHERE v.embedding MATCH ? AND k = ? ORDER BY v.distance"};
    if (not stmt) return ret;
    sqlite3_bind_blob(stmt.get(), 1, query.data(), static_cast<int>(query.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 2, limit);
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        CtSemanticResult r;
        r.chunk_id = sqlite3_column_int64(stmt.get(), 0);
        r.node_id = sqlite3_column_int64(stmt.get(), 1);
        r.text = column_text(stmt.get(), 2);
        r.distance = sqlite3_column_double(stmt.get(), 3);
        ret.push_back(std::move(r));
    }
    if (rc != SQLITE_DONE) spdlog::warn("search index: semantic search: {}", sqlite3_errmsg(_pDb));
    return ret;
}

std::vector<float> CtSearchIndex::node_vector(const gint64 node_id) const
{
    std::vector<float> mean;
    if (not _pDb) return mean;
    Stmt stmt{_pDb, "SELECT v.embedding FROM vec_chunks v JOIN chunks c ON c.chunk_id = v.chunk_id WHERE c.node_id=?"};
    if (not stmt) return mean;
    sqlite3_bind_int64(stmt.get(), 1, node_id);
    int n{0};
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const int bytes = sqlite3_column_bytes(stmt.get(), 0);
        const float* pData = static_cast<const float*>(sqlite3_column_blob(stmt.get(), 0));
        const size_t dim = static_cast<size_t>(bytes) / sizeof(float);
        if (mean.empty()) mean.assign(dim, 0.0f);
        if (mean.size() != dim) continue;
        for (size_t i = 0; i < dim; ++i) mean[i] += pData[i];
        ++n;
    }
    if (n > 0) {
        double norm{0.0};
        for (float& v : mean) { v /= static_cast<float>(n); norm += static_cast<double>(v) * v; }
        norm = std::sqrt(norm);
        if (norm > 0.0) for (float& v : mean) v = static_cast<float>(v / norm);
    }
    return mean;
}

void CtSearchIndex::close()
{
    if (_pDb) {
        sqlite3_close(_pDb);
        _pDb = nullptr;
    }
    _path.clear();
}

bool CtSearchIndex::index_node(const gint64 node_id,
                               const Glib::ustring& name,
                               const Glib::ustring& path,
                               const Glib::ustring& tags,
                               const Glib::ustring& body,
                               const gint64 mtime)
{
    if (not _pDb) return false;
    if (not remove_node(node_id)) return false;
    Stmt stmt{_pDb, "INSERT INTO nodes_fts(node_id, path, mtime, name_raw, body_raw, name, tags, body) VALUES(?,?,?,?,?,?,?,?)"};
    if (not stmt) return false;
    sqlite3_bind_int64(stmt.get(), 1, node_id);
    stmt.bind_text(2, path);
    sqlite3_bind_int64(stmt.get(), 3, mtime);
    stmt.bind_text(4, name);
    stmt.bind_text(5, body);
    stmt.bind_text(6, cjk_space(name));
    stmt.bind_text(7, cjk_space(tags));
    stmt.bind_text(8, cjk_space(body));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        spdlog::error("search index: insert node {}: {}", node_id, sqlite3_errmsg(_pDb));
        return false;
    }
    return true;
}

bool CtSearchIndex::remove_node(const gint64 node_id)
{
    if (not _pDb) return false;
    remove_node_chunks(node_id);
    Stmt stmt{_pDb, "DELETE FROM nodes_fts WHERE node_id=?"};
    if (not stmt) return false;
    sqlite3_bind_int64(stmt.get(), 1, node_id);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool CtSearchIndex::clear()
{
    if (not _pDb) return false;
    _exec("DROP TABLE IF EXISTS vec_chunks");
    _exec("DELETE FROM chunks");
    const int dim = semantic_dim();
    if (dim > 0) _ensure_vec_table(dim);
    return _exec("DELETE FROM nodes_fts");
}

std::map<gint64, gint64> CtSearchIndex::indexed_nodes() const
{
    std::map<gint64, gint64> ret;
    if (not _pDb) return ret;
    Stmt stmt{_pDb, "SELECT node_id, mtime FROM nodes_fts"};
    if (not stmt) return ret;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        ret[sqlite3_column_int64(stmt.get(), 0)] = sqlite3_column_int64(stmt.get(), 1);
    }
    return ret;
}

gint64 CtSearchIndex::count_nodes() const
{
    if (not _pDb) return 0;
    Stmt stmt{_pDb, "SELECT count(*) FROM nodes_fts"};
    if (stmt and sqlite3_step(stmt.get()) == SQLITE_ROW) return sqlite3_column_int64(stmt.get(), 0);
    return 0;
}

/*static*/ bool CtSearchIndex::is_cjk(const gunichar ch)
{
    return (ch >= 0x2E80 and ch <= 0x9FFF)   // radicals, kana, bopomofo, CJK unified
        or (ch >= 0xAC00 and ch <= 0xD7AF)   // hangul syllables
        or (ch >= 0xF900 and ch <= 0xFAFF)   // CJK compatibility
        or (ch >= 0xFF00 and ch <= 0xFFEF)   // fullwidth forms
        or (ch >= 0x20000 and ch <= 0x2FA1F); // CJK extensions
}

/*static*/ Glib::ustring CtSearchIndex::cjk_space(const Glib::ustring& text)
{
    Glib::ustring out;
    out.reserve(text.bytes() * 2);
    bool prev_cjk{false};
    for (const gunichar ch : text) {
        const bool cjk = is_cjk(ch);
        if (cjk or prev_cjk) out += ' ';
        out += ch;
        prev_cjk = cjk;
    }
    return out;
}

/*static*/ std::vector<Glib::ustring> CtSearchIndex::query_terms(const Glib::ustring& user_query)
{
    std::vector<Glib::ustring> terms;
    Glib::ustring current;
    bool in_quotes{false};
    auto push = [&]{ if (not current.empty()) { terms.push_back(current); current.clear(); } };
    for (const gunichar ch : user_query) {
        if (ch == '"') { in_quotes = not in_quotes; push(); continue; }
        if (not in_quotes and (ch == ' ' or ch == '\t' or ch == '\n' or ch == 0x3000)) { push(); continue; }
        current += ch;
    }
    push();
    return terms;
}

/*static*/ Glib::ustring CtSearchIndex::build_fts_query(const Glib::ustring& user_query)
{
    Glib::ustring fts;
    for (const Glib::ustring& term : query_terms(user_query)) {
        bool has_cjk{false};
        for (const gunichar ch : term) if (is_cjk(ch)) { has_cjk = true; break; }
        // a term is a quoted phrase: CJK characters become one token each, so a
        // run of them is a phrase; latin words match by prefix
        Glib::ustring phrase;
        for (const gunichar ch : term) {
            if (ch == '"') continue;
            phrase += ch;
        }
        if (phrase.empty()) continue;
        if (not fts.empty()) fts += " ";
        fts += "\"" + cjk_space(phrase) + "\"";
        if (not has_cjk) fts += "*";
    }
    return fts;
}

/*static*/ Glib::ustring CtSearchIndex::make_snippet(const Glib::ustring& body,
                                                     const std::vector<Glib::ustring>& terms,
                                                     const int window_chars)
{
    const Glib::ustring body_lower = body.lowercase();
    Glib::ustring::size_type best = Glib::ustring::npos;
    // the whole query as a phrase first, then the earliest single term
    if (terms.size() > 1) {
        Glib::ustring phrase;
        for (const Glib::ustring& term : terms) phrase += (phrase.empty() ? "" : " ") + term.lowercase();
        best = body_lower.find(phrase);
    }
    if (best == Glib::ustring::npos) {
        for (const Glib::ustring& term : terms) {
            const auto pos = body_lower.find(term.lowercase());
            if (pos != Glib::ustring::npos and (best == Glib::ustring::npos or pos < best)) best = pos;
        }
    }
    Glib::ustring::size_type start{0};
    if (best != Glib::ustring::npos) {
        start = best > static_cast<Glib::ustring::size_type>(window_chars / 3) ? best - window_chars / 3 : 0;
    }
    // snap to a word/line boundary when possible
    const auto nl = body.rfind('\n', best == Glib::ustring::npos ? 0 : best);
    if (nl != Glib::ustring::npos and nl + 1 >= start and nl + 1 <= (best == Glib::ustring::npos ? 0 : best)) start = nl + 1;
    Glib::ustring snippet = body.substr(start, window_chars);
    const auto cut = snippet.find('\n', best == Glib::ustring::npos ? 0 : (best - start) + 1);
    if (cut != Glib::ustring::npos) snippet = snippet.substr(0, cut);
    // collapse whitespace
    Glib::ustring clean;
    bool prev_space{false};
    for (const gunichar ch : snippet) {
        const bool space = ch == ' ' or ch == '\t' or ch == '\n' or ch == '\r';
        if (space and prev_space) continue;
        clean += space ? ' ' : ch;
        prev_space = space;
    }
    if (start > 0) clean = "…" + clean;
    if (start + window_chars < body.size()) clean += "…";
    return clean;
}

std::vector<CtSearchResult> CtSearchIndex::search(const Glib::ustring& user_query, const int limit) const
{
    std::vector<CtSearchResult> results;
    if (not _pDb) return results;
    const Glib::ustring fts_query = build_fts_query(user_query);
    if (fts_query.empty()) return results;
    // name matches weigh most, then tags, then body
    Stmt stmt{_pDb,
        "SELECT node_id, name_raw, path, body_raw, bm25(nodes_fts, 0, 0, 0, 0, 0, 10.0, 5.0, 1.0) AS rank"
        " FROM nodes_fts WHERE nodes_fts MATCH ? ORDER BY rank LIMIT ?"};
    if (not stmt) return results;
    stmt.bind_text(1, fts_query);
    sqlite3_bind_int(stmt.get(), 2, limit);
    const std::vector<Glib::ustring> terms = query_terms(user_query);
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        CtSearchResult res;
        res.node_id = sqlite3_column_int64(stmt.get(), 0);
        res.node_name = column_text(stmt.get(), 1);
        res.node_path = column_text(stmt.get(), 2);
        res.snippet = make_snippet(column_text(stmt.get(), 3), terms, 120);
        res.rank = sqlite3_column_double(stmt.get(), 4);
        results.push_back(std::move(res));
    }
    if (rc != SQLITE_DONE) {
        spdlog::warn("search index: query '{}' failed: {}", fts_query.raw(), sqlite3_errmsg(_pDb));
    }
    return results;
}
