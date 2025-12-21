#include "xlsx_converter.hpp"
#include <OpenXLSX.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>

static std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string csv_escape(const std::string& s) {
    // CSV с разделителем ';' и кавычками "
    bool need_quotes = false;
    for (char c : s) {
        if (c == ';' || c == '"' || c == '\n' || c == '\r') { need_quotes = true; break; }
    }
    std::string out = s;
    size_t pos = 0;
    while ((pos = out.find('"', pos)) != std::string::npos) {
        out.insert(pos, 1, '"'); // " -> ""
        pos += 2;
    }
    if (need_quotes) out = "\"" + out + "\"";
    return out;
}

static std::string cell_to_string(OpenXLSX::XLWorksheet& ws, int r, int c) {
    try {
        auto cell = ws.cell(r, c);

        // ВАЖНО: proxy нельзя копировать -> берём как ссылку/forwarding ref
        auto&& v = cell.value();

        auto t = v.type();
        switch (t) {
            case OpenXLSX::XLValueType::Empty:
                return "";

            case OpenXLSX::XLValueType::String:
                return trim(v.getString());

            case OpenXLSX::XLValueType::Integer: {
                // В этой версии нет getInteger(), используем шаблонный get<T>()
                // Берём long long чтобы не потерять
                long long n = v.get<long long>();
                return std::to_string(n);
            }

            case OpenXLSX::XLValueType::Float: {
                double d = v.get<double>();
                // уберём хвосты типа 1.000000
                std::ostringstream oss;
                oss << d;
                return trim(oss.str());
            }

            case OpenXLSX::XLValueType::Boolean: {
                bool b = v.get<bool>();
                return b ? "1" : "0";
            }

            default:
                return "";
        }
    } catch (...) {
        return "";
    }
}



static std::string make_ubi_code(long id) {
    std::ostringstream oss;
    oss << "УБИ.";
    if (id < 10) oss << "00";
    else if (id < 100) oss << "0";
    oss << id;
    return oss.str();
}

bool convert_xlsx_to_csv(const std::string& xlsx_path,
                         const std::string& csv_path,
                         std::string& error)
{
    error.clear();
    try {
        OpenXLSX::XLDocument doc;
        doc.open(xlsx_path);

        auto wb = doc.workbook();
        auto ws = wb.worksheet("Sheet"); // в твоём файле лист один и он называется Sheet

        std::ofstream out(csv_path, std::ios::binary);
        if (!out.is_open()) {
            doc.close();
            error = "cannot write csv: " + csv_path;
            return false;
        }

        // Заголовок CSV под нашу БД
        out << "threat_code;title;description;consequences;source\n";

        // В XLSX: строка 2 — заголовки, данные идут с 3-й строки
        for (int r = 3; r <= 50000; ++r) {
            // col1: Идентификатор УБИ (число)
            std::string id_str = cell_to_string(ws, r, 1);
            if (id_str.empty()) continue;

            long id = 0;
            try {
                id = std::stol(id_str);
            } catch (...) {
                continue; // если вдруг не число
            }

            std::string code = make_ubi_code(id);

            // col2: Наименование УБИ
            std::string title = cell_to_string(ws, r, 2);
            // col3: Описание
            std::string desc = cell_to_string(ws, r, 3);
            // col4: Источник угрозы...
            std::string source = cell_to_string(ws, r, 4);

            // col6-8: последствия 0/1
            std::vector<std::string> cons;
            auto c6 = cell_to_string(ws, r, 6);
            auto c7 = cell_to_string(ws, r, 7);
            auto c8 = cell_to_string(ws, r, 8);

            if (c6 == "1") cons.push_back("Нарушение конфиденциальности");
            if (c7 == "1") cons.push_back("Нарушение целостности");
            if (c8 == "1") cons.push_back("Нарушение доступности");

            std::string cons_text;
            for (size_t i = 0; i < cons.size(); ++i) {
                if (i) cons_text += ", ";
                cons_text += cons[i];
            }

            out << csv_escape(code) << ';'
                << csv_escape(title) << ';'
                << csv_escape(desc) << ';'
                << csv_escape(cons_text) << ';'
                << csv_escape(source.empty() ? "fstec" : source)
                << "\n";
        }

        out.close();
        doc.close();
        return true;

    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
