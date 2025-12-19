#pragma once
#include <pqxx/pqxx>
#include <string>

class Db {
public:
    explicit Db(const std::string& conn_str);
    pqxx::connection& conn();
private:
    pqxx::connection conn_;
};
