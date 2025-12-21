#include "xlsx_converter.hpp"

#include <fstream>
#include <string>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <vector>
#include <unordered_map>

#include <OpenXLSX.hpp>

// Утилита: replace all
static void replace_all_inplace(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

// -----------------------
// sanitize_text
// -----------------------
static std::string sanitize_text(std::string s) {
    // Удаляем Excel-вставки вида _x000D_
    replace_all_inplace(s, "_x000D_", "");

    // Заменяем CRLF/CR/LF и таб на пробел
    replace_all_inplace(s, "\r\n", " ");
    replace_all_inplace(s, "\r", " ");
    replace_all_inplace(s, "\n", " ");
    replace_all_inplace(s, "\t", " ");

    // Неразрывный пробел (U+00A0), UTF-8 -> 0xC2 0xA0
    replace_all_inplace(s, std::string("\xC2\xA0", 2), " ");

    // Unicode line separators U+2028 / U+2029 (UTF-8: E2 80 A8 / E2 80 A9)
    replace_all_inplace(s, std::string("\xE2\x80\xA8", 3), " ");
    replace_all_inplace(s, std::string("\xE2\x80\xA9", 3), " ");

    // Преобразуем повторяющиеся пробелы в один.
    std::string out;
    out.reserve(s.size());
    bool prev_space = false;
    for (unsigned char c : s) {
        if (c == ' ') {
            if (!prev_space) {
                out.push_back(' ');
                prev_space = true;
            }
        } else {
            out.push_back(static_cast<char>(c));
            prev_space = false;
        }
    }

    // Трим
    if (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (!out.empty() && out.back() == ' ') out.pop_back();

    return out;
}

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

// Возвращает строковое представление ячейки.
static std::string cell_to_string(const OpenXLSX::XLCell& cell) {
    using OpenXLSX::XLValueType;

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
        // fallback
    }

    try { return v.get<std::string>(); }
    catch (...) { return ""; }
}

// Возвращает строковое представление ячейки с учётом merged-like поведения.
// Если ячейка пуста, пытаемся найти "master" значение:
// 1) ищем вверх по тому же столбцу (row-1,row-2,...)
// 2) если не найдено — ищем влево по той же строке (col-1,col-2,...)
// Это покрывает типичные merged headers и объединения данных.
static std::string cell_to_string_with_merged(OpenXLSX::XLWorksheet& ws, uint32_t row, uint16_t col) {
    using OpenXLSX::XLCellReference;
    auto cell = ws.cell(XLCellReference(row, col));
    std::string s = cell_to_string(cell);
    if (!s.empty()) return s;

    // 1) Ищем вверх по тому же столбцу
    for (int rr = (int)row - 1; rr >= 1; --rr) {
        try {
            auto c = ws.cell(XLCellReference((uint32_t)rr, col));
            s = cell_to_string(c);
            if (!s.empty()) return s;
        } catch (...) {
            // игнорируем ошибки (на всякий)
        }
    }

    // 2) Ищем влево по той же строке
    for (int cc = (int)col - 1; cc >= 1; --cc) {
        try {
            auto c = ws.cell(XLCellReference(row, (uint16_t)cc));
            s = cell_to_string(c);
            if (!s.empty()) return s;
        } catch (...) {
            // игнорируем
        }
    }

    // ничего не найдено
    return "";
}

// Проверка: строка пустая, если в первых maxColToCheck колонках нет значений
static bool is_row_empty(OpenXLSX::XLWorksheet& ws, uint32_t row, uint16_t maxColToCheck) {
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
        // 1) Найдём строку заголовков динамически:
        // Ищем в первых N строк ячейку с "Идентификатор"/"УБИ"/"UBI"
        // ----------------------------
        const uint32_t headerSearchLimit = 30; // поиск в первых 30 строк
        uint32_t headerRow = 1;
        bool header_found = false;
        for (uint32_t r = 1; r <= headerSearchLimit; ++r) {
            for (uint16_t c = 1; c <= 20; ++c) { // по первым 20 столбцам (запас)
                std::string v = cell_to_string_with_merged(ws, r, c);
                if (v.empty()) continue;
                // Ищем ключевые слова (рус/англ)
                if (v.find("Идентификатор") != std::string::npos ||
                    v.find("идентификатор") != std::string::npos ||
                    v.find("УБИ") != std::string::npos ||
                    v.find("уби") != std::string::npos ||
                    v.find("UBI") != std::string::npos ||
                    v.find("ubi") != std::string::npos ||
                    v.find("Идент") != std::string::npos) {
                    headerRow = r;
                    header_found = true;
                    break;
                }
            }
            if (header_found) break;
        }

        // ----------------------------
        // 2) Вычисляем кол-во колонок (учитываем пару строк заголовка)
        // ----------------------------
        uint16_t maxCols = 1;
        int emptyStreak = 0;
        const int emptyLimit = 3;
        const uint16_t hardMaxCols = 400;

        for (uint16_t col = 1; col <= hardMaxCols; ++col) {
            bool anyNonEmpty = false;
            for (uint32_t rr = headerRow; rr <= headerRow + 2; ++rr) {
                std::string v = cell_to_string_with_merged(ws, rr, col);
                if (!v.empty()) { anyNonEmpty = true; break; }
            }
            if (!anyNonEmpty) emptyStreak++;
            else {
                emptyStreak = 0;
                maxCols = col;
            }
            if (emptyStreak >= emptyLimit) break;
        }

        // ----------------------------
        // 3) Собираем имена колонок (объединяем headerRow и headerRow+1)
        // ----------------------------
        std::vector<std::string> columnNames;
        columnNames.reserve(maxCols);
        for (uint16_t c = 1; c <= maxCols; ++c) {
            std::string name;
            for (uint32_t rr = headerRow; rr <= headerRow + 1; ++rr) {
                std::string part = cell_to_string_with_merged(ws, rr, c);
                part = sanitize_text(std::move(part));
                if (!part.empty()) {
                    if (!name.empty()) name += " ";
                    name += part;
                }
            }
            columnNames.push_back(name);
        }

        // ----------------------------
        // 4) Сопоставляем колонки с нужными полями
        // ----------------------------
        std::unordered_map<std::string, int> fieldIndex;
        fieldIndex.reserve(5);

        auto contains_any = [&](const std::string &h, const std::vector<std::string>& pats) -> bool {
            for (const auto &p : pats) {
                if (h.find(p) != std::string::npos) return true;
            }
            return false;
        };

        std::vector<std::string> p_threat_code = {"Идентификатор", "идентификатор", "УБИ", "уби", "UBI", "ubi", "Код", "код"};
        std::vector<std::string> p_title = {"Наименование", "наименование", "Название", "название", "Наим", "Title", "title"};
        std::vector<std::string> p_description = {"Описание", "описание", "Description", "description"};
        std::vector<std::string> p_consequences = {"Последств", "последств", "Последствия", "последствия", "Consequences", "consequences"};
        std::vector<std::string> p_source = {"Источник", "источник", "Source", "source"};

        for (size_t i = 0; i < columnNames.size(); ++i) {
            const std::string &h = columnNames[i];
            if (fieldIndex.find("threat_code") == fieldIndex.end() && contains_any(h, p_threat_code)) {
                fieldIndex["threat_code"] = (int)i; continue;
            }
            if (fieldIndex.find("title") == fieldIndex.end() && contains_any(h, p_title)) {
                fieldIndex["title"] = (int)i; continue;
            }
            if (fieldIndex.find("description") == fieldIndex.end() && contains_any(h, p_description)) {
                fieldIndex["description"] = (int)i; continue;
            }
            if (fieldIndex.find("consequences") == fieldIndex.end() && contains_any(h, p_consequences)) {
                fieldIndex["consequences"] = (int)i; continue;
            }
            if (fieldIndex.find("source") == fieldIndex.end() && contains_any(h, p_source)) {
                fieldIndex["source"] = (int)i; continue;
            }
        }

        // fallback на первые 5 колонок
        if (fieldIndex.find("threat_code") == fieldIndex.end()) fieldIndex["threat_code"] = 0;
        if (fieldIndex.find("title") == fieldIndex.end()) fieldIndex["title"] = (maxCols >= 2 ? 1 : 0);
        if (fieldIndex.find("description") == fieldIndex.end()) fieldIndex["description"] = (maxCols >= 3 ? 2 : 0);
        if (fieldIndex.find("consequences") == fieldIndex.end()) fieldIndex["consequences"] = (maxCols >= 4 ? 3 : 0);
        if (fieldIndex.find("source") == fieldIndex.end()) fieldIndex["source"] = (maxCols >= 5 ? 4 : 0);

        // ----------------------------
        // 5) Вычисляем кол-во строк данных
        // ----------------------------
        uint32_t dataStartRow = headerRow + 1;
        uint32_t maxRows = dataStartRow;
        emptyStreak = 0;
        const uint32_t hardMaxRows = 200000;
        for (uint32_t row = dataStartRow; row <= hardMaxRows; ++row) {
            bool empty = is_row_empty(ws, row, (uint16_t)std::min<int>(maxCols, 2));
            if (empty) emptyStreak++;
            else {
                emptyStreak = 0;
                maxRows = row;
            }
            if (emptyStreak >= 30) break;
        }

        // ----------------------------
        // 6) Пишем CSV: фиксированный заголовок + данные в нужном порядке
        // ----------------------------
        out << "threat_code;title;description;consequences;source\n";

        for (uint32_t r = dataStartRow; r <= maxRows; ++r) {
            bool rowAllEmpty = true;

            std::vector<std::string> outCols(5);
            for (int fi = 0; fi < 5; ++fi) {
                std::string fieldName;
                switch (fi) {
                    case 0: fieldName = "threat_code"; break;
                    case 1: fieldName = "title"; break;
                    case 2: fieldName = "description"; break;
                    case 3: fieldName = "consequences"; break;
                    case 4: fieldName = "source"; break;
                }
                int colIdx = fieldIndex[fieldName]; // zero-based
                uint16_t col = (uint16_t)colIdx + 1;
                std::string s = cell_to_string_with_merged(ws, r, col);
                s = sanitize_text(std::move(s));
                if (!s.empty()) rowAllEmpty = false;
                outCols[fi] = std::move(s);
            }

            // --- НОВОЕ: нормализация threat_code ---
            {
                // трим функция
                auto trim_inplace = [](std::string &t){
                    size_t i = 0;
                    while (i < t.size() && std::isspace((unsigned char)t[i])) ++i;
                    size_t j = t.size();
                    while (j > i && std::isspace((unsigned char)t[j-1])) --j;
                    t = t.substr(i, j - i);
                };

                std::string code = outCols[0];
                trim_inplace(code);

                // Удалим NBSP если есть (UTF-8 C2 A0)
                replace_all_inplace(code, std::string("\xC2\xA0", 2), " ");

                // replace latin UBI. -> Cyrillic УБИ.
                if (code.rfind("UBI.", 0) == 0 || code.rfind("ubi.", 0) == 0) {
                    code.replace(0, 4, std::string("УБИ."));
                }

                // Если код — только цифры, форматируем УБИ.###
                bool all_digits = !code.empty();
                for (char ch : code) {
                    if (!std::isdigit((unsigned char)ch)) { all_digits = false; break; }
                }
                if (all_digits) {
                    int n = 0;
                    try { n = std::stoi(code); } catch(...) { n = 0; }
                    char buf[16];
                    snprintf(buf, sizeof(buf), "УБИ.%03d", n);
                    outCols[0] = std::string(buf);
                } else {
                    // иначе используем нормализованный и триммированный код
                    outCols[0] = code;
                }
            }

            // пропускаем полностью пустые строки
            if (rowAllEmpty) continue;

            // убеждаемся, что threat_code не пуст
            if (outCols[0].empty()) continue;

            for (int i = 0; i < 5; ++i) {
                out << csv_escape(outCols[i]);
                if (i != 4) out << ';';
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
