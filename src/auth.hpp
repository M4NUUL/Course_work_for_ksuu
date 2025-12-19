#pragma once
#include <string>

class Db;

struct User {
    long long id = 0;
    std::string login;
    std::string email;
    bool is_admin = false;
};

class AuthService {
public:
    explicit AuthService(Db& db);

    bool register_user(const std::string& login,
                       const std::string& email,
                       const std::string& password,
                       std::string& error);

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
