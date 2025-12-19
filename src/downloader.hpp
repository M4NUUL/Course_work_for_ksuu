#pragma once
#include <string>

// Скачивает URL в filepath. Возвращает true при успехе, иначе false и error заполнен.
bool download_https(const std::string& url, const std::string& filepath, std::string& error);
