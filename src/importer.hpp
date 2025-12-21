#pragma once
#include <string>

class Db;

struct ImportStats {
    int inserted = 0;
    int updated  = 0;
};

bool import_threats_csv(Db& db,
                        const std::string& csv_path,
                        long user_id,
                        ImportStats& stats,
                        std::string& error);
