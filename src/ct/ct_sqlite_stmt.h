/*
 * ct_sqlite_stmt.h
 *
 * Small RAII helpers around sqlite3_stmt shared by the search index sources.
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

#include "ct_logging.h"
#include <glibmm/ustring.h>
#include <sqlite3.h>

namespace CtSqlite {

/// RAII prepared statement
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
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    operator bool() const { return nullptr != _pStmt; }
    sqlite3_stmt* get() { return _pStmt; }
    void bind_text(const int idx, const Glib::ustring& text) {
        sqlite3_bind_text(_pStmt, idx, text.c_str(), static_cast<int>(text.bytes()), SQLITE_TRANSIENT);
    }
    void bind_int64(const int idx, const gint64 value) { sqlite3_bind_int64(_pStmt, idx, value); }
    void bind_int(const int idx, const int value) { sqlite3_bind_int(_pStmt, idx, value); }
    bool step_done() { return sqlite3_step(_pStmt) == SQLITE_DONE; }
    bool step_row() { return sqlite3_step(_pStmt) == SQLITE_ROW; }
private:
    sqlite3_stmt* _pStmt{nullptr};
};

inline Glib::ustring column_text(sqlite3_stmt* pStmt, const int col)
{
    const unsigned char* pText = sqlite3_column_text(pStmt, col);
    return pText ? Glib::ustring{reinterpret_cast<const char*>(pText)} : Glib::ustring{};
}

} // namespace CtSqlite
