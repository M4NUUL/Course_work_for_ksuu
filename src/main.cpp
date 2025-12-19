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
  #include <termios.h>
  #include <unistd.h>
#endif

#include "db.hpp"
#include "auth.hpp"
#include "threats.hpp"
#include "search_session.hpp"

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
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
    std::cout << "\n";
}

// Скрытый ввод пароля (без отображения символов)
static std::string read_password(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();

    std::string pass;

#ifdef _WIN32
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') { // Enter
            std::cout << "\n";
            break;
        }
        if (ch == '\b') { // Backspace
            if (!pass.empty()) pass.pop_back();
            continue;
        }
        if (ch == 0 || ch == 224) { // спецклавиши
            _getch();
            continue;
        }
        pass.push_back(static_cast<char>(ch));
    }
#else
    termios oldt{};
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        std::getline(std::cin, pass);
        return pass;
    }

    termios newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::getline(std::cin, pass);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
#endif

    return pass;
}

static bool is_valid_ubi_code(const std::string& input) {
    auto trim = [](std::string s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        return s;
    };

    std::string s = trim(input);

    const std::string p1 = "УБИ.";
    const std::string p2 = "UBI.";

    std::size_t prefix_len = 0;
    if (s.rfind(p1, 0) == 0) prefix_len = p1.size();
    else if (s.rfind(p2, 0) == 0) prefix_len = p2.size();
    else return false;

    if (s.size() != prefix_len + 3) return false;

    return std::isdigit((unsigned char)s[prefix_len]) &&
           std::isdigit((unsigned char)s[prefix_len + 1]) &&
           std::isdigit((unsigned char)s[prefix_len + 2]);
}

static std::string normalize_ubi_code(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();

    if (s.rfind("UBI.", 0) == 0) {
        s.replace(0, 4, "УБИ.");
    }
    return s;
}

static void show_main_menu(const std::string& login) {
    std::cout << "Добро пожаловать, " << login << "!\n\n";
    std::cout <<
        "1. Найти угрозу по идентификатору УБИ\n"
        "2. Поиск по ключевому слову (добавить в отчёт)\n"
        "3. Экспорт отчёта в CSV\n"
        "4. Выход\n"
        "> ";
}

static bool auth_screen(AuthService& auth, User& out_user) {
    while (true) {
        std::cout <<
            "1. Войти\n"
            "2. Зарегистрироваться\n"
            "3. Выход\n"
            "> ";

        int choice = 0;
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (choice == 1) {
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
        }

        if (choice == 2) {
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
        }

        if (choice == 3) {
            return false;
        }

        std::cout << "Неверный выбор.\n\n";
    }
}

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
            show_main_menu(current.login);

            int cmd = 0;
            std::cin >> cmd;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (cmd == 4) {
                std::cout << "Выход...\n";
                break;
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
                if (!t) {
                    std::cout << "Угроза не найдена.\n\n";
                    continue;
                }

                std::cout << "\n" << t->code << " — " << t->title << "\n";
                std::cout << "Описание: " << t->description << "\n";
                std::cout << "Последствия: " << t->consequences << "\n";
                std::cout << "Источник: " << t->source << "\n\n";
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
                          << ", всего в отчёте: " << session.size() << "\n";

                std::cout << "Результаты (первые 20):\n";
                int shown = 0;
                for (const auto& t : found) {
                    std::cout << " - " << t.code << " — " << t.title << "\n";
                    if (++shown >= 20) break;
                }
                std::cout << "\n";
                continue;
            }

            if (cmd == 3) {
                if (session.size() == 0) {
                    std::cout << "Отчёт пуст. Сначала добавьте угрозы через поиск по слову.\n\n";
                    continue;
                }

                std::string path = "exports/selected_threats.csv";

#ifdef _WIN32
                system("mkdir exports >nul 2>nul");
#else
                system("mkdir -p exports >/dev/null 2>/dev/null");
#endif

                std::string err;
                if (!session.export_csv(path, err)) {
                    std::cout << "Ошибка экспорта: " << err << "\n\n";
                    continue;
                }

                std::cout << "Экспорт выполнен: " << path << "\n\n";
                continue;
            }

            std::cout << "Неизвестная команда.\n\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
