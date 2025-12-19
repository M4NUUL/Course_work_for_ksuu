#pragma once
#include <string>
#include <optional>

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

private:
    Db& db_;
};
