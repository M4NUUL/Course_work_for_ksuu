#pragma once
#include <map>
#include <string>
#include <vector>

struct Threat;

class SearchSession {
public:
    // добавляет угрозы (без дублей по code). Возвращает сколько новых добавлено.
    int add(const std::vector<Threat>& items);

    // экспорт текущего набора в CSV (отсортировано по threat_code)
    bool export_csv(const std::string& filepath, std::string& error) const;

    std::size_t size() const { return items_.size(); }

private:
    // map сортирует ключи сам => сразу порядок по threat_code
    std::map<std::string, Threat> items_;
};
