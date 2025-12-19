#include "threats.hpp"
#include "db.hpp"
#include <pqxx/pqxx>

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
