#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <limits>
#include <cctype>

#ifdef _WIN32
  #include <windows.h>
  #include <conio.h>
#else
  #include <termios.h>   // tcgetattr, tcsetattr, struct termios, ECHO, TCSANOW
  #include <unistd.h>    // STDIN_FILENO
#endif

#include <pqxx/pqxx>

#include "db.hpp"
#include "auth.hpp"
#include "threats.hpp"
#include "search_session.hpp"
#include "downloader.hpp"
#include "xlsx_converter.hpp"
#include "importer.hpp"

// ---------------- helper functions ----------------
static void set_console_utf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

static void show_banner() {
    std::cout <<
        "========================================\n"
        "        БАЗА ДАННЫХ УГРОЗ ФСТЭК\n"
        "========================================\n\n";
}

static void fake_loading() {
    std::cout << "Загрузка";
    for (int i = 0; i < 3; ++i) {
        std::cout << ".";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::cout << "\n";
}

static std::string read_password(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();

    std::string pass;

#ifdef _WIN32
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            std::cout << "\n";
            break;
        }
        if (ch == '\b') {
            if (!pass.empty()) pass.pop_back();
            continue;
        }
        if (ch == 0 || ch == 224) {
            _getch();
            continue;
        }
        pass.push_back(static_cast<char>(ch));
    }
#else
    struct termios oldt{};
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        std::getline(std::cin, pass);
        return pass;
    }

    struct termios newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::getline(std::cin, pass);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
#endif

    return pass;
}

static bool is_valid_ubi_code(const std::string& input) {
    std::string s = input;
    // trim
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    const std::string p1 = "УБИ.";
    const std::string p2 = "UBI.";
    size_t prefix_len = 0;
    if (s.rfind(p1, 0) == 0) prefix_len = p1.size();
    else if (s.rfind(p2, 0) == 0) prefix_len = p2.size();
    else return false;
    if (s.size() != prefix_len + 3) return false;
    return std::isdigit((unsigned char)s[prefix_len]) &&
           std::isdigit((unsigned char)s[prefix_len+1]) &&
           std::isdigit((unsigned char)s[prefix_len+2]);
}

static std::string normalize_ubi_code(std::string s) {
    // trim
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    if (s.rfind("UBI.", 0) == 0) s.replace(0,4,"УБИ.");
    return s;
}

// show menu
static void show_main_menu(const User& current) {
    std::cout << "Добро пожаловать, " << current.login
              << "  [роль: " << current.role << "]\n\n";
    std::cout <<
        "1. Найти угрозу по УБИ\n"
        "2. Поиск по ключевому слову\n"
        "3. Экспорт отчёта\n"
        "4. Обновить банк угроз\n"
        "5. История обновлений\n"
        "6. Управление пользователями (admin)\n"
        "7. Добавить угрозу вручную (admin/editor)\n"
        "8. Выход\n"
        "> ";
}

static void ensure_data_dirs() {
#ifdef _WIN32
    system("mkdir data >nul 2>nul");
    system("mkdir exports >nul 2>nul");
#else
    system("mkdir -p data >/dev/null 2>/dev/null");
    system("mkdir -p exports >/dev/null 2>/dev/null");
#endif
}

// download & convert only (no DB import here)
static bool download_and_convert_csv(std::string& out_err) {
    const std::string url = "https://bdu.fstec.ru/files/documents/thrlist.xlsx";
    const std::string xlsx_path = "data/thrlist.xlsx";
    const std::string csv_path  = "data/thrlist.csv";

    ensure_data_dirs();

    std::cout << "Скачивание XLSX...\n";
    if (!download_https(url, xlsx_path, out_err)) {
        return false;
    }
    std::cout << "OK: " << xlsx_path << "\n";

    std::cout << "Конвертация XLSX -> CSV...\n";
    if (!convert_xlsx_to_csv(xlsx_path, csv_path, out_err)) {
        return false;
    }
    std::cout << "OK: " << csv_path << "\n";

    return true;
}

// admin: manage users
static void manage_users(Db& db) {
    while (true) {
        std::cout << "\n=== Управление пользователями ===\n";
        std::cout << "1. Показать пользователей\n";
        std::cout << "2. Изменить роль пользователя\n";
        std::cout << "3. Выйти\n> ";

        std::string choice;
        std::getline(std::cin, choice);
        if (choice == "1") {
            try {
                pqxx::work tx(db.conn());
                auto r = tx.exec("SELECT id, login, role, is_admin FROM users ORDER BY id");
                for (const auto& row : r) {
                    std::cout << row[0].as<long long>() << " | "
                              << row[1].c_str() << " | role=" << row[2].c_str()
                              << " | is_admin=" << (row[3].as<bool>() ? "true" : "false") << "\n";
                }
            } catch (const std::exception& e) {
                std::cout << "Ошибка: " << e.what() << "\n";
            }
        } else if (choice == "2") {
            std::string login, role;
            std::cout << "Введите логин пользователя: ";
            std::getline(std::cin, login);
            std::cout << "Введите роль (admin/editor/viewer): ";
            std::getline(std::cin, role);
            if (role != "admin" && role != "editor" && role != "viewer") {
                std::cout << "Неверная роль.\n";
                continue;
            }
            try {
                pqxx::work tx(db.conn());
                tx.exec_params("UPDATE users SET role = $1, is_admin = $2 WHERE login = $3",
                               role, (role == "admin"), login);
                tx.commit();
                std::cout << "Роль обновлена.\n";
            } catch (const std::exception& e) {
                std::cout << "Ошибка: " << e.what() << "\n";
            }
        } else if (choice == "3") {
            break;
        } else {
            std::cout << "Неверный выбор.\n";
        }
    }
}

// auth screen (login/register)
static bool auth_screen(AuthService& auth, User& out_user) {
    while (true) {
        std::cout <<
            "1. Войти\n"
            "2. Зарегистрироваться\n"
            "3. Выход\n"
            "> ";
        std::string line;
        std::getline(std::cin, line);
        if (line.empty()) continue;
        if (line == "1") {
            std::string login;
            std::cout << "Логин: ";
            std::getline(std::cin, login);
            std::string pass = read_password("Пароль: ");
            std::string err;
            if (!auth.login_user(login, pass, out_user, err)) {
                std::cout << "Ошибка: " << err << "\n\n";
                continue;
            }
            return true;
        } else if (line == "2") {
            std::string login, email;
            std::cout << "Новый логин: ";
            std::getline(std::cin, login);
            std::cout << "Email: ";
            std::getline(std::cin, email);
            std::string pass = read_password("Пароль: ");
            std::string err;
            if (!auth.register_user(login, email, pass, err)) {
                std::cout << "Ошибка регистрации: " << err << "\n\n";
                continue;
            }
            std::cout << "Регистрация успешна. Теперь войдите.\n\n";
            continue;
        } else if (line == "3") {
            return false;
        } else {
            std::cout << "Неверный выбор.\n\n";
        }
    }
}

// ---------------- main ----------------
int main() {
    set_console_utf8();
    clear_screen();
    show_banner();
    fake_loading();
    clear_screen();

    try {
        Db db("host=localhost port=5432 dbname=bdu user=bdu_app password=bdu_pass");
        AuthService auth(db);
        ThreatRepository threats(db);
        SearchSession session;

        User current;
        if (!auth_screen(auth, current)) {
            std::cout << "Выход...\n";
            return 0;
        }

        while (true) {
            show_main_menu(current);

            std::string choice;
            if (!std::getline(std::cin, choice)) { // EOF
                std::cout << "\nВыход...\n";
                break;
            }
            // trim
            size_t i = 0;
            while (i < choice.size() && std::isspace((unsigned char)choice[i])) ++i;
            size_t j = choice.size();
            while (j > i && std::isspace((unsigned char)choice[j-1])) --j;
            choice = choice.substr(i, j - i);

            if (choice.empty()) {
                std::cout << "Пустой ввод.\n\n";
                continue;
            }

            // If direct UBI code provided
            std::string possible_code = choice;
            if (possible_code.rfind("UBI.", 0) == 0 || possible_code.rfind("ubi.", 0) == 0) {
                possible_code.replace(0,4,"УБИ.");
            }
            if (is_valid_ubi_code(possible_code)) {
                std::string code = normalize_ubi_code(possible_code);
                auto t = threats.get_by_code(code);
                if (!t) std::cout << "Угроза не найдена.\n\n";
                else {
                    std::cout << "\n" << t->code << " — " << t->title << "\n";
                    std::cout << "Описание: " << t->description << "\n";
                    std::cout << "Последствия: " << t->consequences << "\n";
                    std::cout << "Источник: " << t->source << "\n\n";
                }
                continue;
            }

            // parse command number
            int cmd = 0;
            try {
                size_t idx = 0;
                cmd = std::stoi(choice, &idx);
                if (idx != choice.size()) throw std::invalid_argument("trailing");
            } catch (...) {
                std::cout << "Неизвестная команда.\n\n";
                continue;
            }

            if (cmd == 1) {
                std::string code;
                std::cout << "Введите УБИ-код (пример: УБИ.001): ";
                std::getline(std::cin, code);
                code = normalize_ubi_code(code);
                if (!is_valid_ubi_code(code)) {
                    std::cout << "Ошибка: неверный формат. Используйте УБИ.001\n\n";
                    continue;
                }
                auto t = threats.get_by_code(code);
                if (!t) std::cout << "Угроза не найдена.\n\n";
                else {
                    std::cout << "\n" << t->code << " — " << t->title << "\n";
                    std::cout << "Описание: " << t->description << "\n";
                    std::cout << "Последствия: " << t->consequences << "\n";
                    std::cout << "Источник: " << t->source << "\n\n";
                }
                continue;
            }

            if (cmd == 2) {
                std::string keyword;
                std::cout << "Введите ключевое слово (пример: wifi): ";
                std::getline(std::cin, keyword);
                if (keyword.empty()) {
                    std::cout << "Пустой запрос.\n\n";
                    continue;
                }
                auto found = threats.search_by_keyword(keyword);
                if (found.empty()) {
                    std::cout << "Ничего не найдено.\n\n";
                    continue;
                }
                int added = session.add(found);
                std::cout << "Найдено: " << found.size()
                          << ", добавлено в отчёт (новых): " << added
                          << ", всего в отчёте: " << session.size() << "\n\n";
                continue;
            }

            if (cmd == 3) {
                if (session.size() == 0) {
                    std::cout << "Отчёт пуст. Сначала добавьте угрозы через поиск по слову.\n\n";
                    continue;
                }
                ensure_data_dirs();
                std::string path = "exports/selected_threats.csv";
                std::string err;
                if (!session.export_csv(path, err)) {
                    std::cout << "Ошибка экспорта: " << err << "\n\n";
                } else {
                    std::cout << "Экспорт выполнен: " << path << "\n\n";
                }
                continue;
            }

            if (cmd == 4) {
                if (!current.is_admin()) {
                    std::cout << "Недостаточно прав. Команда доступна только admin.\n\n";
                    continue;
                }
                std::string err;
                if (!download_and_convert_csv(err)) {
                    std::cout << "Ошибка обновления: " << err << "\n\n";
                    continue;
                }
                // теперь импортируем
                ImportStats stats;
                std::string imp_err;
                if (!import_threats_csv(db, "data/thrlist.csv", current.id, stats, imp_err)) {
                    std::cout << "Ошибка импорта: " << imp_err << "\n\n";
                } else {
                    std::cout << "Импорт завершён. Добавлено: " << stats.inserted << ", Обновлено: " << stats.updated << "\n\n";

                    // запишем лог обновления
                    try {
                        pqxx::work tx(db.conn());
                        tx.exec_params(
                            "INSERT INTO update_log(user_id, inserted, updated, source) VALUES($1,$2,$3,$4)",
                            current.id, stats.inserted, stats.updated, std::string("official:thrlist.xlsx")
                        );
                        tx.commit();
                    } catch (const std::exception& e) {
                        // не фатально, но покажем ошибку
                        std::cout << "Не удалось записать update_log: " << e.what() << "\n\n";
                    }
                }

                continue;
            }

            if (cmd == 5) {
                try {
                    pqxx::work tx(db.conn());
                    auto r = tx.exec(
                        "SELECT ul.imported_at, u.login, ul.inserted, ul.updated, ul.source "
                        "FROM update_log ul JOIN users u ON u.id = ul.user_id "
                        "ORDER BY ul.imported_at DESC LIMIT 20"
                    );
                    for (const auto& row : r) {
                        std::cout << row[0].c_str() << " | " << row[1].c_str()
                                  << " | added: " << row[2].as<int>()
                                  << " | updated: " << row[3].as<int>()
                                  << " | source: " << row[4].c_str() << "\n";
                    }
                    std::cout << "\n";
                } catch (const std::exception& e) {
                    std::cout << "Ошибка: " << e.what() << "\n\n";
                }
                continue;
            }

            if (cmd == 6) {
                if (!current.is_admin()) {
                    std::cout << "Недостаточно прав. Только admin.\n\n";
                    continue;
                }
                manage_users(db);
                continue;
            }

            if (cmd == 7) {
                if (!current.can_add()) {
                    std::cout << "Недостаточно прав. Только admin и editor могут добавлять угрозы.\n\n";
                    continue;
                }

                Threat t;
                std::cout << "Введите код (пример: УБИ.001): ";
                std::getline(std::cin, t.code);
                t.code = normalize_ubi_code(t.code);

                if (t.code.empty()) {
                    std::cout << "Код не введён.\n\n";
                    continue;
                }

                std::cout << "Введите наименование (title): ";
                std::getline(std::cin, t.title);

                std::cout << "Введите описание: ";
                std::getline(std::cin, t.description);

                std::cout << "Введите последствия: ";
                std::getline(std::cin, t.consequences);

                std::cout << "Введите источник: ";
                std::getline(std::cin, t.source);

                std::string err;
                if (!threats.insert_threat(t, current.id, err)) {
    std::cout << "Не удалось добавить угрозу: " << err << "\n\n";
} else {
    std::cout << "Угроза успешно добавлена: " << t.code << "\n\n";

    // Запишем в update_log: один вставлен, zero обновлён
    try {
        pqxx::work tx(db.conn());
        tx.exec_params(
            "INSERT INTO update_log(user_id, inserted, updated, source) VALUES($1,$2,$3,$4)",
            current.id,
            1,   // inserted
            0,   // updated
            std::string("manual:add") // источник
        );
        tx.commit();
    } catch (const std::exception &e) {
        // Лог не критичен — просто сообщаем админу
        std::cout << "Предупреждение: не удалось записать update_log: " << e.what() << "\n\n";
    }
}

                continue;
            }

            if (cmd == 8) {
                std::cout << "Выход...\n";
                break;
            }

            std::cout << "Неизвестная команда.\n\n";
        }

        

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
