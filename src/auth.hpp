#pragma once
#include <string>

class Db;

// Представление пользователя в приложении.
// Роль хранится в виде строки: "admin" | "editor" | "viewer".
struct User {
    long long id = 0;
    std::string login;
    std::string email;

    // Роль: admin | editor | viewer
    std::string role = "viewer";

    // Утилиты доступа
    bool is_admin() const { return role == "admin"; }
    bool can_add()  const { return role == "admin" || role == "editor"; }
    bool can_read() const { return true; }
};

class AuthService {
public:
    explicit AuthService(Db& db);

    // Регистрирует пользователя. Если это первый пользователь в БД —
    // по умолчанию даст роль "admin", иначе "viewer".
    bool register_user(const std::string& login,
                       const std::string& email,
                       const std::string& password,
                       std::string& error);

    // Логин: заполняет out_user (включая role) при успехе.
    bool login_user(const std::string& login,
                    const std::string& password,
                    User& out_user,
                    std::string& error) const;

private:
    Db& db_;

    bool is_password_strong(const std::string& password, std::string& error) const;
    bool user_exists(const std::string& login) const;
    bool is_first_user() const;
};
