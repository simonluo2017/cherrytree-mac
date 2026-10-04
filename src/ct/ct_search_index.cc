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
#include <algorithm>

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

bool CtSearchIndex::open(const fs::path& index_path, std::string* pError)
{
    close();
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
        "INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '1');";
    if (not _exec(schema, pError)) {
        close();
        return false;
    }
    return true;
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
    Stmt stmt{_pDb, "DELETE FROM nodes_fts WHERE node_id=?"};
    if (not stmt) return false;
    sqlite3_bind_int64(stmt.get(), 1, node_id);
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool CtSearchIndex::clear()
{
    if (not _pDb) return false;
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
