#include "threats.hpp"
#include "db.hpp"
#include <pqxx/pqxx>
#include <vector>

ThreatRepository::ThreatRepository(Db& db) : db_(db) {}

bool ThreatRepository::insert_threat(const Threat& t, long user_id, std::string& error) {
    if (t.code.empty()) {
        error = "threat_code пустой";
        return false;
    }

    try {
        pqxx::work tx(db_.conn());
        // Попытка вставить; если уже есть — DO NOTHING
        auto r = tx.exec_params(R"SQL(
            INSERT INTO threats(
                threat_code, title, description, consequences, source, created_by, created_at
            ) VALUES(
                $1, $2, $3, $4, $5, $6, now()
            )
            ON CONFLICT (threat_code) DO NOTHING
            RETURNING threat_code
        )SQL",
        t.code, t.title, t.description, t.consequences, t.source, user_id);

        tx.commit();

        if (r.empty()) {
            error = "Угроза с таким кодом уже существует";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

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
