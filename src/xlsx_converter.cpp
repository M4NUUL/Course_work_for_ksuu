#include "xlsx_converter.hpp"

#include <fstream>
#include <string>
#include <stdexcept>
#include <cctype>

#include <OpenXLSX.hpp>

static std::string csv_escape(const std::string& s) {
    // Разделитель ; (удобно для Excel/русской локали)
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

static std::string cell_to_string(const OpenXLSX::XLCell& cell) {
    using OpenXLSX::XLValueType;

    // Берём ссылку на proxy, НЕ копируем
    const auto& v = cell.value();

    try {
        switch (v.type()) {
            case XLValueType::Empty:
                return "";
            case XLValueType::Boolean:
                return v.get<bool>() ? "true" : "false";
            case XLValueType::Integer:
                return std::to_string(v.get<int64_t>());
            case XLValueType::Float:
                return std::to_string(v.get<double>());
            case XLValueType::String:
                return v.get<std::string>();
            case XLValueType::Error:
                return "#ERROR";
            default:
                break;
        }
    } catch (...) {
        // игнор, пойдём в fallback
    }

    try { return v.get<std::string>(); }
    catch (...) { return ""; }
}


static bool is_row_empty(OpenXLSX::XLWorksheet& ws, uint32_t row, uint16_t maxColToCheck) {
    // Считаем строку пустой, если в первых maxColToCheck колонках всё пусто
    for (uint16_t col = 1; col <= maxColToCheck; ++col) {
        auto cell = ws.cell(OpenXLSX::XLCellReference(row, col));
        if (!cell_to_string(cell).empty()) return false;
    }
    return true;
}

bool convert_xlsx_to_csv(const std::string& xlsx_path,
                         const std::string& csv_path,
                         std::string& error)
{
    try {
        OpenXLSX::XLDocument doc;
        doc.open(xlsx_path);

        auto wb = doc.workbook();
        auto sheetNames = wb.worksheetNames();
        if (sheetNames.empty()) {
            doc.close();
            error = "XLSX: нет листов в файле";
            return false;
        }

        auto ws = wb.worksheet(sheetNames.at(0));   // первый лист

        std::ofstream out(csv_path, std::ios::binary);
        if (!out) {
            doc.close();
            error = "Не удалось открыть CSV для записи: " + csv_path;
            return false;
        }

        // ----------------------------
        // ВЫЧИСЛЯЕМ КОЛ-ВО КОЛОНОК:
        // Берём 1-ю строку (заголовки) и идём вправо пока не встретили пустоту N раз подряд.
        // ----------------------------
        const uint32_t headerRow = 1;
        uint16_t maxCols = 1;
        int emptyStreak = 0;
        const int emptyLimit = 3;          // 3 пустых подряд -> конец
        const uint16_t hardMaxCols = 200;  // защита

        for (uint16_t col = 1; col <= hardMaxCols; ++col) {
            auto v = cell_to_string(ws.cell(OpenXLSX::XLCellReference(headerRow, col)));
            if (v.empty()) emptyStreak++;
            else {
                emptyStreak = 0;
                maxCols = col;
            }
            if (emptyStreak >= emptyLimit) break;
        }

        // ----------------------------
        // ВЫЧИСЛЯЕМ КОЛ-ВО СТРОК:
        // Идём вниз, пока первая колонка (или первые 2) не пустые N раз подряд.
        // ----------------------------
        uint32_t maxRows = 1;
        emptyStreak = 0;
        const uint32_t hardMaxRows = 200000; // защита на всякий

        for (uint32_t row = 1; row <= hardMaxRows; ++row) {
            // Для БДУ обычно первая колонка не должна быть пустой (код/ID)
            // Но на всякий проверим первые 2 колонки
            bool empty = is_row_empty(ws, row, (uint16_t)std::min<int>(maxCols, 2));
            if (empty) emptyStreak++;
            else {
                emptyStreak = 0;
                maxRows = row;
            }
            if (emptyStreak >= 30) break; // 30 пустых строк подряд -> конец
        }

        // ----------------------------
        // ПИШЕМ CSV: rows x cols
        // ----------------------------
        for (uint32_t r = 1; r <= maxRows; ++r) {
            for (uint16_t c = 1; c <= maxCols; ++c) {
                auto cell = ws.cell(OpenXLSX::XLCellReference(r, c));
                std::string s = cell_to_string(cell);

                out << csv_escape(s);
                if (c != maxCols) out << ';';
            }
            out << "\n";
        }

        doc.close();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "Unknown error in convert_xlsx_to_csv";
        return false;
    }
}
