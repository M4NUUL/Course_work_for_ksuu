#pragma once
#include <string>
#include <optional>
#include <vector>

class Db;

struct Threat {
    std::string code;         // УБИ.001
    std::string title;
    std::string description;
    std::string consequences;
    std::string source;
};

class ThreatRepository {
public:
    explicit ThreatRepository(Db& db);
    std::optional<Threat> get_by_code(const std::string& threat_code);

    std::vector<Threat> search_by_keyword(const std::string& keyword, int limit = 200);
private:
    Db& db_;
};
