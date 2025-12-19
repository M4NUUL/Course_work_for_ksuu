#include "threats.hpp"
#include "db.hpp"
#include <pqxx/pqxx>
#include <vector>

ThreatRepository::ThreatRepository(Db& db) : db_(db) {}

std::optional<Threat> ThreatRepository::get_by_code(const std::string& threat_code) {
    pqxx::work tx(db_.conn());
    auto r = tx.exec_params(
        "SELECT threat_code, title, COALESCE(description,''), COALESCE(consequences,''), COALESCE(source,'') "
        "FROM threats WHERE threat_code = $1",
        threat_code
    );
    tx.commit();

    if (r.empty()) return std::nullopt;

    Threat t;
    t.code = r[0][0].c_str();
    t.title = r[0][1].c_str();
    t.description = r[0][2].c_str();
    t.consequences = r[0][3].c_str();
    t.source = r[0][4].c_str();
    return t;
}

std::vector<Threat> ThreatRepository::search_by_keyword(const std::string& keyword, int limit) {
    // Ищем по title и description, без full-text пока (проще и достаточно).
    // Позже можно перейти на tsvector.
    std::string pattern = "%" + keyword + "%";

    pqxx::work tx(db_.conn());
    auto r = tx.exec_params(
        "SELECT threat_code, title, COALESCE(description,''), COALESCE(consequences,''), COALESCE(source,'') "
        "FROM threats "
        "WHERE title ILIKE $1 OR description ILIKE $1 "
        "ORDER BY threat_code "
        "LIMIT $2",
        pattern, limit
    );
    tx.commit();

    std::vector<Threat> out;
    out.reserve(r.size());

    for (const auto& row : r) {
        Threat t;
        t.code = row[0].c_str();
        t.title = row[1].c_str();
        t.description = row[2].c_str();
        t.consequences = row[3].c_str();
        t.source = row[4].c_str();
        out.push_back(std::move(t));
    }
    return out;
}
