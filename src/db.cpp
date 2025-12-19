#include "db.hpp"
#include <stdexcept>

Db::Db(const std::string& conn_str) : conn_(conn_str) {
    if (!conn_.is_open()) {
        throw std::runtime_error("Не удалось открыть соединение с PostgreSQL");
    }
}

pqxx::connection& Db::conn() {
    return conn_;
}
