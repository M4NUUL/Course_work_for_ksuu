#include "importer.hpp"
#include "db.hpp"

#include <pqxx/pqxx>
#include <fstream>
#include <string>
#include <vector>

// Определяем разделитель по заголовку: что чаще, ';' или ','
static char detect_delim(const std::string& header_line) {
    int sc = 0, cc = 0;
    for (char ch : header_line) {
        if (ch == ';') sc++;
        if (ch == ',') cc++;
    }
    return (sc > cc) ? ';' : ',';
}

// CSV parser с кавычками + параметр delimiter
static std::vector<std::string> parse_csv_line(const std::string& line, char delim) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];

        if (in_quotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cur.push_back(ch);
            }
        } else {
            if (ch == '"') {
                in_quotes = true;
            } else if (ch == delim) {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
    }
    out.push_back(cur);
    return out;
}

bool import_threats_csv(Db& db,
                        const std::string& csv_path,
                        long user_id,
                        ImportStats& stats,
                        std::string& error)
{
    error.clear();
    stats = {};

    try {
        pqxx::work tx(db.conn());

        // 1) staging должен существовать
        tx.exec("TRUNCATE staging_threats");

        std::ifstream file(csv_path);
        if (!file.is_open()) {
            error = "Не удалось открыть CSV: " + csv_path;
            return false;
        }

        // 2) читаем заголовок и определяем разделитель
        std::string header_line;
        if (!std::getline(file, header_line)) {
            error = "CSV пустой: " + csv_path;
            return false;
        }
        char delim = detect_delim(header_line);

        // 3) заливаем строки в staging
        auto stream = pqxx::stream_to::table(
            tx,
            pqxx::table_path{"staging_threats"},
            {"threat_code", "title", "description", "consequences", "source"}
        );

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            auto cols = parse_csv_line(line, delim);
            if (cols.size() < 5) continue;

            stream << std::make_tuple(cols[0], cols[1], cols[2], cols[3], cols[4]);
        }

        stream.complete();

        // 4) дедуп по threat_code + UPSERT
        auto res = tx.exec_params(R"SQL(
            WITH dedup AS (
                SELECT DISTINCT ON (threat_code)
                    threat_code, title, description, consequences, source
                FROM staging_threats
                WHERE threat_code IS NOT NULL AND threat_code <> ''
                ORDER BY threat_code
            )
            INSERT INTO threats(
                threat_code, title, description, consequences, source,
                created_by, created_at
            )
            SELECT
                threat_code, title, description, consequences, source,
                $1, now()
            FROM dedup
            ON CONFLICT (threat_code) DO UPDATE
            SET
                title        = EXCLUDED.title,
                description  = EXCLUDED.description,
                consequences = EXCLUDED.consequences,
                source       = EXCLUDED.source,
                updated_by   = $1,
                updated_at   = now()
            RETURNING (xmax = 0)::int AS inserted;
        )SQL", user_id);

        for (auto const& row : res) {
            int inserted_flag = row[0].as<int>();
            if (inserted_flag == 1) stats.inserted++;
            else stats.updated++;
        }

tx.exec_params(
  "INSERT INTO update_log(user_id, inserted, updated, source) VALUES($1,$2,$3,$4)",
  user_id, stats.inserted, stats.updated, csv_path
);
 
        tx.commit();
        return true;

    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
