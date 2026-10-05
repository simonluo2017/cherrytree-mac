/*
 * ct_search_index_graph.cc
 *
 * Knowledge graph part of the search index: entities and relations that the
 * local model extracts from the chunks, with their provenance (chunk, node).
 * The graph is derived data like the rest of the index: it can be dropped and
 * rebuilt at any time, the notes are never modified.
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
#include "ct_sqlite_stmt.h"
#include "ct_logging.h"

#include <algorithm>
#include <set>

using CtSqlite::Stmt;
using CtSqlite::column_text;

namespace {

Glib::ustring trim_field(const Glib::ustring& in)
{
    Glib::ustring s = in;
    auto is_junk = [](const gunichar ch){
        return ch == ' ' or ch == '\t' or ch == '\r' or ch == '\n' or ch == '"' or ch == '\'' or ch == '`'
            or ch == 0x201C or ch == 0x201D or ch == 0x2018 or ch == 0x2019 or ch == 0x3000 or ch == '*';
    };
    while (not s.empty() and is_junk(s[0])) s = s.substr(1);
    while (not s.empty() and is_junk(s[s.size()-1])) s = s.substr(0, s.size()-1);
    // trailing sentence punctuation
    while (not s.empty() and (s[s.size()-1] == '.' or s[s.size()-1] == 0x3002)) s = s.substr(0, s.size()-1);
    return s;
}

Glib::ustring normalize_type(const Glib::ustring& in)
{
    Glib::ustring t = trim_field(in).lowercase();
    static const std::map<Glib::ustring, Glib::ustring> synonyms{
        {"people", "person"}, {"persons", "person"}, {"human", "person"}, {"user", "person"}, {"人", "person"}, {"人物", "person"},
        {"org", "organization"}, {"organisation", "organization"}, {"company", "organization"}, {"team", "organization"}, {"组织", "organization"}, {"公司", "organization"},
        {"location", "place"}, {"city", "place"}, {"country", "place"}, {"地点", "place"}, {"地方", "place"},
        {"tool", "software"}, {"app", "software"}, {"application", "software"}, {"library", "software"}, {"framework", "software"}, {"软件", "software"},
        {"server", "system"}, {"service", "system"}, {"infrastructure", "system"}, {"系统", "system"},
        {"technology", "concept"}, {"topic", "concept"}, {"term", "concept"}, {"idea", "concept"}, {"概念", "concept"},
        {"meeting", "event"}, {"事件", "event"}, {"time", "date"}, {"日期", "date"},
        {"项目", "project"}, {"产品", "product"}, {"其他", "other"}, {"", "other"}};
    const auto it = synonyms.find(t);
    if (it != synonyms.end()) return it->second;
    if (t.size() > 24) return "other";
    return t;
}

bool is_word_char(const gunichar ch)
{
    return g_unichar_isalnum(ch) or ch == '_';
}

} // namespace

/*static*/ std::string CtSearchIndex::normalize_entity_name(const Glib::ustring& name)
{
    Glib::ustring out;
    bool prev_space{false};
    for (const gunichar ch : trim_field(name).lowercase()) {
        const bool space = ch == ' ' or ch == '\t' or ch == '\n' or ch == '\r' or ch == 0x3000;
        if (space and prev_space) continue;
        out += space ? ' ' : ch;
        prev_space = space;
    }
    return out.raw();
}

/*static*/ CtGraphExtraction CtSearchIndex::parse_extraction(const Glib::ustring& model_output)
{
    CtGraphExtraction result;
    std::map<std::string, size_t> entity_index; // normalized name -> position in result.entities
    auto add_entity = [&](const Glib::ustring& raw_name, const Glib::ustring& raw_type, const Glib::ustring& raw_desc)->bool{
        const Glib::ustring name = trim_field(raw_name);
        if (name.empty() or name.size() > 80) return false;
        const std::string norm = normalize_entity_name(name);
        if (norm.empty() or norm == "none") return false;
        const Glib::ustring type = normalize_type(raw_type);
        const Glib::ustring desc = trim_field(raw_desc);
        auto found = entity_index.find(norm);
        if (found == entity_index.end()) {
            entity_index[norm] = result.entities.size();
            result.entities.push_back(CtGraphExtraction::Entity{name, type, desc.size() > 300 ? desc.substr(0, 300) : desc});
        }
        else {
            CtGraphExtraction::Entity& e = result.entities[found->second];
            if ((e.type.empty() or e.type == "other") and type != "other") e.type = type;
            if (e.description.empty()) e.description = desc;
        }
        return true;
    };
    std::vector<CtGraphExtraction::Relation> relations;
    Glib::ustring::size_type start{0};
    while (start <= model_output.size()) {
        const auto nl = model_output.find('\n', start);
        Glib::ustring line = nl == Glib::ustring::npos ? model_output.substr(start) : model_output.substr(start, nl - start);
        start = nl == Glib::ustring::npos ? model_output.size() + 1 : nl + 1;
        line = trim_field(line);
        if (line.empty() or line.find("```") == 0) continue;
        // list markers and numbering
        if (line.size() > 2 and (line[0] == '-' or line[0] == 0x2022) and line[1] == ' ') line = line.substr(2);
        {
            Glib::ustring::size_type i{0};
            while (i < line.size() and g_unichar_isdigit(line[i])) ++i;
            if (i > 0 and i + 1 < line.size() and (line[i] == '.' or line[i] == ')') and line[i+1] == ' ') line = line.substr(i + 2);
        }
        // split on the pipe (fullwidth pipe accepted)
        std::vector<Glib::ustring> fields;
        {
            Glib::ustring current;
            for (const gunichar ch : line) {
                if (ch == '|' or ch == 0xFF5C) { fields.push_back(trim_field(current)); current.clear(); }
                else current += ch;
            }
            fields.push_back(trim_field(current));
        }
        if (fields.size() < 2) continue;
        Glib::ustring kind = fields[0].uppercase();
        while (not kind.empty() and (kind[kind.size()-1] == ':' or kind[kind.size()-1] == 0xFF1A)) kind = kind.substr(0, kind.size()-1);
        if (kind == "ENTITY" or kind == "实体" or kind == "E") {
            add_entity(fields[1], fields.size() > 2 ? fields[2] : Glib::ustring{}, fields.size() > 3 ? fields[3] : Glib::ustring{});
        }
        else if ((kind == "RELATION" or kind == "关系" or kind == "R") and fields.size() >= 4) {
            CtGraphExtraction::Relation r;
            r.source = trim_field(fields[1]);
            r.type = trim_field(fields[2]).lowercase();
            r.target = trim_field(fields[3]);
            if (fields.size() > 4) r.description = trim_field(fields[4]);
            if (r.source.empty() or r.target.empty() or r.type.empty()) continue;
            if (r.source.size() > 80 or r.target.size() > 80 or r.type.size() > 60) continue;
            if (normalize_entity_name(r.source) == normalize_entity_name(r.target)) continue;
            if (r.description.size() > 300) r.description = r.description.substr(0, 300);
            relations.push_back(r);
        }
    }
    // relations may name entities that were not listed: they become entities of unknown type
    std::set<std::string> seen_relations;
    for (const CtGraphExtraction::Relation& r : relations) {
        if (not add_entity(r.source, "other", "") and entity_index.count(normalize_entity_name(r.source)) == 0) continue;
        if (not add_entity(r.target, "other", "") and entity_index.count(normalize_entity_name(r.target)) == 0) continue;
        const std::string key = normalize_entity_name(r.source) + "\x01" + r.type.raw() + "\x01" + normalize_entity_name(r.target);
        if (seen_relations.insert(key).second) result.relations.push_back(r);
    }
    return result;
}

bool CtSearchIndex::_graph_create_schema(std::string* pError)
{
    const char* schema =
        "CREATE TABLE IF NOT EXISTS entities("
        "  entity_id INTEGER PRIMARY KEY, name TEXT NOT NULL, name_norm TEXT NOT NULL UNIQUE,"
        "  type TEXT NOT NULL DEFAULT '', description TEXT NOT NULL DEFAULT '');"
        "CREATE TABLE IF NOT EXISTS entity_mentions("
        "  entity_id INTEGER NOT NULL, node_id INTEGER NOT NULL, chunk_id INTEGER NOT NULL,"
        "  PRIMARY KEY(entity_id, chunk_id));"
        "CREATE INDEX IF NOT EXISTS mentions_chunk ON entity_mentions(chunk_id);"
        "CREATE INDEX IF NOT EXISTS mentions_node ON entity_mentions(node_id);"
        "CREATE TABLE IF NOT EXISTS relations("
        "  relation_id INTEGER PRIMARY KEY, source_id INTEGER NOT NULL, target_id INTEGER NOT NULL,"
        "  type TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', node_id INTEGER NOT NULL, chunk_id INTEGER NOT NULL,"
        "  UNIQUE(source_id, target_id, type, chunk_id));"
        "CREATE INDEX IF NOT EXISTS relations_source ON relations(source_id);"
        "CREATE INDEX IF NOT EXISTS relations_target ON relations(target_id);"
        "CREATE INDEX IF NOT EXISTS relations_chunk ON relations(chunk_id);";
    if (not _exec(schema, pError)) return false;
    // indexes created before the graph existed lack the extracted flag on the chunks
    bool has_extracted{false};
    {
        Stmt stmt{_pDb, "PRAGMA table_info(chunks)"};
        while (stmt and stmt.step_row()) {
            if (column_text(stmt.get(), 1) == "extracted") { has_extracted = true; break; }
        }
    }
    if (not has_extracted and not _exec("ALTER TABLE chunks ADD COLUMN extracted INTEGER NOT NULL DEFAULT 0", pError)) return false;
    return _exec("CREATE INDEX IF NOT EXISTS chunks_extracted ON chunks(extracted)", pError);
}

void CtSearchIndex::_graph_forget_chunk(const gint64 chunk_id)
{
    Stmt m{_pDb, "DELETE FROM entity_mentions WHERE chunk_id=?"};
    if (m) { m.bind_int64(1, chunk_id); m.step_done(); }
    Stmt r{_pDb, "DELETE FROM relations WHERE chunk_id=?"};
    if (r) { r.bind_int64(1, chunk_id); r.step_done(); }
}

void CtSearchIndex::_graph_prune()
{
    _exec("DELETE FROM entities WHERE entity_id NOT IN (SELECT entity_id FROM entity_mentions)"
          " AND entity_id NOT IN (SELECT source_id FROM relations) AND entity_id NOT IN (SELECT target_id FROM relations)");
}

gint64 CtSearchIndex::_graph_entity_id(const Glib::ustring& name, const Glib::ustring& type, const Glib::ustring& description)
{
    const std::string norm = normalize_entity_name(name);
    if (norm.empty()) return 0;
    {
        Stmt stmt{_pDb, "SELECT entity_id, type, description FROM entities WHERE name_norm=?"};
        if (not stmt) return 0;
        stmt.bind_text(1, norm);
        if (stmt.step_row()) {
            const gint64 id = sqlite3_column_int64(stmt.get(), 0);
            const Glib::ustring old_type = column_text(stmt.get(), 1);
            const Glib::ustring old_desc = column_text(stmt.get(), 2);
            const bool better_type = (old_type.empty() or old_type == "other") and not type.empty() and type != "other";
            const bool better_desc = old_desc.empty() and not description.empty();
            if (better_type or better_desc) {
                Stmt upd{_pDb, "UPDATE entities SET type=?, description=? WHERE entity_id=?"};
                if (upd) {
                    upd.bind_text(1, better_type ? type : old_type);
                    upd.bind_text(2, better_desc ? description : old_desc);
                    upd.bind_int64(3, id);
                    upd.step_done();
                }
            }
            return id;
        }
    }
    Stmt ins{_pDb, "INSERT INTO entities(name, name_norm, type, description) VALUES(?,?,?,?)"};
    if (not ins) return 0;
    ins.bind_text(1, trim_field(name));
    ins.bind_text(2, norm);
    ins.bind_text(3, type.empty() ? Glib::ustring{"other"} : type);
    ins.bind_text(4, description);
    if (not ins.step_done()) return 0;
    return sqlite3_last_insert_rowid(_pDb);
}

bool CtSearchIndex::graph_store_extraction(const gint64 chunk_id, const gint64 node_id, const CtGraphExtraction& extraction)
{
    if (not _pDb) return false;
    {
        // the chunk may have been replaced while the model was working on it
        Stmt stmt{_pDb, "SELECT 1 FROM chunks WHERE chunk_id=?"};
        if (not stmt) return false;
        stmt.bind_int64(1, chunk_id);
        if (not stmt.step_row()) return false;
    }
    _exec("BEGIN");
    _graph_forget_chunk(chunk_id);
    std::map<std::string, gint64> ids;
    auto entity_id = [&](const Glib::ustring& name, const Glib::ustring& type, const Glib::ustring& desc)->gint64{
        const std::string norm = normalize_entity_name(name);
        auto found = ids.find(norm);
        if (found != ids.end()) return found->second;
        const gint64 id = _graph_entity_id(name, type, desc);
        if (id > 0) {
            ids[norm] = id;
            Stmt m{_pDb, "INSERT OR IGNORE INTO entity_mentions(entity_id, node_id, chunk_id) VALUES(?,?,?)"};
            if (m) { m.bind_int64(1, id); m.bind_int64(2, node_id); m.bind_int64(3, chunk_id); m.step_done(); }
        }
        return id;
    };
    for (const CtGraphExtraction::Entity& e : extraction.entities) entity_id(e.name, e.type, e.description);
    for (const CtGraphExtraction::Relation& r : extraction.relations) {
        const gint64 source = entity_id(r.source, "other", "");
        const gint64 target = entity_id(r.target, "other", "");
        if (source <= 0 or target <= 0 or source == target) continue;
        Stmt ins{_pDb, "INSERT OR IGNORE INTO relations(source_id, target_id, type, description, node_id, chunk_id) VALUES(?,?,?,?,?,?)"};
        if (not ins) continue;
        ins.bind_int64(1, source);
        ins.bind_int64(2, target);
        ins.bind_text(3, r.type);
        ins.bind_text(4, r.description);
        ins.bind_int64(5, node_id);
        ins.bind_int64(6, chunk_id);
        ins.step_done();
    }
    {
        Stmt upd{_pDb, "UPDATE chunks SET extracted=1 WHERE chunk_id=?"};
        if (upd) { upd.bind_int64(1, chunk_id); upd.step_done(); }
    }
    _graph_prune();
    return _exec("COMMIT");
}

bool CtSearchIndex::graph_mark_extracted(const gint64 chunk_id)
{
    if (not _pDb) return false;
    Stmt upd{_pDb, "UPDATE chunks SET extracted=1 WHERE chunk_id=?"};
    if (not upd) return false;
    upd.bind_int64(1, chunk_id);
    return upd.step_done();
}

std::vector<CtChunk> CtSearchIndex::graph_pending_chunks(const int limit) const
{
    std::vector<CtChunk> ret;
    if (not _pDb) return ret;
    Stmt stmt{_pDb, "SELECT chunk_id, node_id, seq, start_offset, text FROM chunks WHERE extracted=0 ORDER BY chunk_id LIMIT ?"};
    if (not stmt) return ret;
    stmt.bind_int(1, limit);
    while (stmt.step_row()) {
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

gint64 CtSearchIndex::graph_count_pending() const   { return _count("SELECT count(*) FROM chunks WHERE extracted=0"); }
gint64 CtSearchIndex::graph_count_extracted() const { return _count("SELECT count(*) FROM chunks WHERE extracted=1"); }
gint64 CtSearchIndex::graph_count_entities() const  { return _count("SELECT count(*) FROM entities"); }
gint64 CtSearchIndex::graph_count_relations() const { return _count("SELECT count(*) FROM relations"); }

bool CtSearchIndex::graph_clear()
{
    if (not _pDb) return false;
    return _exec("DELETE FROM relations; DELETE FROM entity_mentions; DELETE FROM entities; UPDATE chunks SET extracted=0;");
}

std::string CtSearchIndex::graph_model_name() const
{
    return _pDb ? _get_meta("graph_model") : std::string{};
}

bool CtSearchIndex::graph_set_model_name(const std::string& name)
{
    return _pDb and _set_meta("graph_model", name);
}

namespace {
CtGraphEntity read_entity(sqlite3_stmt* pStmt)
{
    CtGraphEntity e;
    e.entity_id = sqlite3_column_int64(pStmt, 0);
    e.name = column_text(pStmt, 1);
    e.type = column_text(pStmt, 2);
    e.description = column_text(pStmt, 3);
    e.mentions = sqlite3_column_int(pStmt, 4);
    return e;
}
const char* ENTITY_COLUMNS = "e.entity_id, e.name, e.type, e.description, (SELECT count(*) FROM entity_mentions m WHERE m.entity_id=e.entity_id)";
}

std::vector<CtGraphEntity> CtSearchIndex::graph_entities(const Glib::ustring& filter, const int limit) const
{
    std::vector<CtGraphEntity> ret;
    if (not _pDb) return ret;
    const std::string sql = std::string{"SELECT "} + ENTITY_COLUMNS + " AS n FROM entities e WHERE (? = '' OR instr(e.name_norm, ?) > 0) ORDER BY n DESC, e.name LIMIT ?";
    Stmt stmt{_pDb, sql.c_str()};
    if (not stmt) return ret;
    const std::string norm = normalize_entity_name(filter);
    stmt.bind_text(1, norm);
    stmt.bind_text(2, norm);
    stmt.bind_int(3, limit);
    while (stmt.step_row()) ret.push_back(read_entity(stmt.get()));
    return ret;
}

std::vector<CtGraphEntity> CtSearchIndex::graph_entities_of_node(const gint64 node_id) const
{
    std::vector<CtGraphEntity> ret;
    if (not _pDb) return ret;
    const std::string sql = std::string{"SELECT "} + ENTITY_COLUMNS + " AS n FROM entities e WHERE e.entity_id IN (SELECT entity_id FROM entity_mentions WHERE node_id=?) ORDER BY n DESC, e.name";
    Stmt stmt{_pDb, sql.c_str()};
    if (not stmt) return ret;
    stmt.bind_int64(1, node_id);
    while (stmt.step_row()) ret.push_back(read_entity(stmt.get()));
    return ret;
}

bool CtSearchIndex::graph_entity(const gint64 entity_id, CtGraphEntity& out) const
{
    if (not _pDb) return false;
    const std::string sql = std::string{"SELECT "} + ENTITY_COLUMNS + " FROM entities e WHERE e.entity_id=?";
    Stmt stmt{_pDb, sql.c_str()};
    if (not stmt) return false;
    stmt.bind_int64(1, entity_id);
    if (not stmt.step_row()) return false;
    out = read_entity(stmt.get());
    return true;
}

std::vector<CtGraphRelation> CtSearchIndex::graph_relations_of(const gint64 entity_id) const
{
    std::vector<CtGraphRelation> ret;
    if (not _pDb) return ret;
    Stmt stmt{_pDb,
        "SELECT r.relation_id, r.source_id, r.target_id, s.name, t.name, r.type, r.description, r.node_id, r.chunk_id"
        " FROM relations r JOIN entities s ON s.entity_id=r.source_id JOIN entities t ON t.entity_id=r.target_id"
        " WHERE r.source_id=? OR r.target_id=? ORDER BY r.type, s.name, t.name"};
    if (not stmt) return ret;
    stmt.bind_int64(1, entity_id);
    stmt.bind_int64(2, entity_id);
    while (stmt.step_row()) {
        CtGraphRelation r;
        r.relation_id = sqlite3_column_int64(stmt.get(), 0);
        r.source_id = sqlite3_column_int64(stmt.get(), 1);
        r.target_id = sqlite3_column_int64(stmt.get(), 2);
        r.source_name = column_text(stmt.get(), 3);
        r.target_name = column_text(stmt.get(), 4);
        r.type = column_text(stmt.get(), 5);
        r.description = column_text(stmt.get(), 6);
        r.node_id = sqlite3_column_int64(stmt.get(), 7);
        r.chunk_id = sqlite3_column_int64(stmt.get(), 8);
        ret.push_back(std::move(r));
    }
    return ret;
}

std::vector<std::pair<gint64, int>> CtSearchIndex::graph_nodes_of_entity(const gint64 entity_id) const
{
    std::vector<std::pair<gint64, int>> ret;
    if (not _pDb) return ret;
    Stmt stmt{_pDb, "SELECT node_id, count(*) AS n FROM entity_mentions WHERE entity_id=? GROUP BY node_id ORDER BY n DESC"};
    if (not stmt) return ret;
    stmt.bind_int64(1, entity_id);
    while (stmt.step_row()) ret.emplace_back(sqlite3_column_int64(stmt.get(), 0), sqlite3_column_int(stmt.get(), 1));
    return ret;
}

std::vector<CtGraphEntity> CtSearchIndex::graph_match_entities(const Glib::ustring& text, const int limit) const
{
    std::vector<CtGraphEntity> ret;
    if (not _pDb or text.empty()) return ret;
    const Glib::ustring haystack = Glib::ustring{normalize_entity_name(text)};
    const std::string sql = std::string{"SELECT "} + ENTITY_COLUMNS + ", e.name_norm FROM entities e";
    Stmt stmt{_pDb, sql.c_str()};
    if (not stmt) return ret;
    while (stmt.step_row()) {
        const Glib::ustring needle = column_text(stmt.get(), 5);
        if (needle.size() < 2) continue;
        Glib::ustring::size_type pos = haystack.find(needle);
        bool matched{false};
        while (pos != Glib::ustring::npos and not matched) {
            // latin names must match whole words (an "it" entity must not match "with")
            const bool starts_word = pos == 0 or not is_word_char(haystack[pos-1]) or not is_word_char(needle[0]);
            const auto end = pos + needle.size();
            const bool ends_word = end >= haystack.size() or not is_word_char(haystack[end]) or not is_word_char(needle[needle.size()-1]);
            matched = starts_word and ends_word;
            if (not matched) pos = haystack.find(needle, pos + 1);
        }
        if (matched) ret.push_back(read_entity(stmt.get()));
    }
    // longer names first (more specific), then the better known entities
    std::sort(ret.begin(), ret.end(), [](const CtGraphEntity& a, const CtGraphEntity& b){
        if (a.name.size() != b.name.size()) return a.name.size() > b.name.size();
        return a.mentions > b.mentions;
    });
    if (static_cast<int>(ret.size()) > limit) ret.resize(limit);
    return ret;
}

std::vector<std::pair<gint64, double>> CtSearchIndex::graph_chunks_of_entities(const std::map<gint64, double>& entity_weights, const int limit) const
{
    std::vector<std::pair<gint64, double>> ret;
    if (not _pDb or entity_weights.empty()) return ret;
    std::string in_list;
    for (const auto& [id, w] : entity_weights) { if (not in_list.empty()) in_list += ","; in_list += std::to_string(id); }
    const std::string sql = "SELECT chunk_id, entity_id FROM entity_mentions WHERE entity_id IN (" + in_list + ")";
    Stmt stmt{_pDb, sql.c_str()};
    if (not stmt) return ret;
    std::map<gint64, double> scores;
    while (stmt.step_row()) {
        const gint64 chunk_id = sqlite3_column_int64(stmt.get(), 0);
        const gint64 entity_id = sqlite3_column_int64(stmt.get(), 1);
        const auto w = entity_weights.find(entity_id);
        if (w != entity_weights.end()) scores[chunk_id] += w->second;
    }
    for (const auto& [chunk_id, score] : scores) ret.emplace_back(chunk_id, score);
    std::sort(ret.begin(), ret.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    if (static_cast<int>(ret.size()) > limit) ret.resize(limit);
    return ret;
}
