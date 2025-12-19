#include "search_session.hpp"
#include "threats.hpp"

#include <fstream>

int SearchSession::add(const std::vector<Threat>& items) {
    int added = 0;
    for (const auto& t : items) {
        auto [it, inserted] = items_.emplace(t.code, t);
        if (inserted) added++;
    }
    return added;
}

static std::string csv_escape(const std::string& s) {
    // CSV: экранируем кавычки, если есть ; или " или перенос строки
    bool need_quotes = false;
    for (char c : s) {
        if (c == ';' || c == '"' || c == '\n' || c == '\r') { need_quotes = true; break; }
    }
    if (!need_quotes) return s;

    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

bool SearchSession::export_csv(const std::string& filepath, std::string& error) const {
    std::ofstream f(filepath, std::ios::binary);
    if (!f) {
        error = "Не удалось открыть файл для записи: " + filepath;
        return false;
    }

    // Разделитель ; (удобно для русской локали Excel)
    f << "threat_code;title;description;consequences;source\n";
    for (const auto& [code, t] : items_) {
        f << csv_escape(t.code) << ';'
          << csv_escape(t.title) << ';'
          << csv_escape(t.description) << ';'
          << csv_escape(t.consequences) << ';'
          << csv_escape(t.source) << '\n';
    }
    return true;
}
