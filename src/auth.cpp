#include "auth.hpp"
#include "db.hpp"
#include "crypto.hpp"

#include <pqxx/pqxx>
#include <cctype>

AuthService::AuthService(Db& db) : db_(db) {}

bool AuthService::is_password_strong(const std::string& password, std::string& error) const {
    if (password.size() < 8) {
        error = "Пароль должен быть не короче 8 символов.";
        return false;
    }
    bool has_letter = false, has_digit = false, has_special = false;
    for (unsigned char ch : password) {
        if (std::isalpha(ch)) has_letter = true;
        else if (std::isdigit(ch)) has_digit = true;
        else has_special = true;
    }
    if (!has_letter || !has_digit || !has_special) {
        error = "Пароль должен содержать буквы, цифры и спецсимвол.";
        return false;
    }
    return true;
}

bool AuthService::user_exists(const std::string& login) const {
    pqxx::work tx(db_.conn());
    auto r = tx.exec_params("SELECT 1 FROM users WHERE login = $1", login);
    tx.commit();
    return !r.empty();
}

bool AuthService::is_first_user() const {
    pqxx::work tx(db_.conn());
    auto r = tx.exec("SELECT COUNT(*) AS c FROM users");
    tx.commit();
    return r[0]["c"].as<long long>() == 0;
}

bool AuthService::register_user(const std::string& login,
                                const std::string& email,
                                const std::string& password,
                                std::string& error) {
    if (login.empty()) { error = "Логин не должен быть пустым."; return false; }
    if (email.empty() || email.find('@') == std::string::npos) { error = "Некорректный email."; return false; }
    if (!is_password_strong(password, error)) return false;
    if (user_exists(login)) { error = "Пользователь с таким логином уже существует."; return false; }

    const std::string salt = random_salt_hex(16);
    const std::string hash = sha256_hex(password + salt);
    const bool first = is_first_user(); // первый пользователь — админ
    const std::string role = first ? "admin" : "viewer";

    pqxx::work tx(db_.conn());
    tx.exec_params(
        "INSERT INTO users(login, email, password_hash, salt, is_admin, role) VALUES($1,$2,$3,$4,$5,$6)",
        login, email, hash, salt, first, role
    );
    tx.commit();

    return true;
}

bool AuthService::login_user(const std::string& login,
                             const std::string& password,
                             User& out_user,
                             std::string& error) const {
    pqxx::work tx(db_.conn());
    auto r = tx.exec_params(
        "SELECT id, login, email, password_hash, salt, is_admin, role FROM users WHERE login = $1",
        login
    );
    tx.commit();

    if (r.empty()) { error = "Неверный логин или пароль."; return false; }

    const std::string salt = r[0]["salt"].c_str();
    const std::string expected = r[0]["password_hash"].c_str();
    const std::string got = sha256_hex(password + salt);

    if (got != expected) { error = "Неверный логин или пароль."; return false; }

    out_user.id    = r[0]["id"].as<long long>();
    out_user.login = r[0]["login"].c_str();
    out_user.email = r[0]["email"].c_str();

    try {
        out_user.role = r[0]["role"].as<std::string>();
        if (out_user.role.empty()) out_user.role = r[0]["is_admin"].as<bool>() ? "admin" : "viewer";
    } catch (...) {
        out_user.role = r[0]["is_admin"].as<bool>() ? "admin" : "viewer";
    }

    return true;
}
