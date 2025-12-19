#pragma once
#include <string>

// Конвертирует 1-й лист XLSX в CSV.
// Возвращает true при успехе, иначе false и error заполнен.
bool convert_xlsx_to_csv(const std::string& xlsx_path,
                         const std::string& csv_path,
                         std::string& error);
