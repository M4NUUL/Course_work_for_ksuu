#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif

#include "auth.hpp"
#include "db.hpp"

#ifdef _WIN32
  #include <conio.h>
#else
  #include <termios.h>
  #include <unistd.h>
#endif

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

static void show_main_menu(const std::string& login) {
    std::cout << "Добро пожаловать, " << login << "!\n\n";
    std::cout <<
        "1. Поиск угроз\n"
        "2. Управление данными\n"
        "3. Отчёты и экспорт\n"
        "4. Выход\n"
        "> ";
}

static std::string read_password(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();

    std::string pass;

#ifdef _WIN32
    // Windows: читаем посимвольно, не показываем ввод
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') { // Enter
            std::cout << "\n";
            break;
        }
        if (ch == '\b') { // Backspace
            if (!pass.empty()) {
                pass.pop_back();
                // Если захочешь звездочки — надо удалять их с экрана.
            }
            continue;
        }
        // Игнорируем спецклавиши (стрелки и т.п.)
        if (ch == 0 || ch == 224) {
            _getch();
            continue;
        }
        pass.push_back(static_cast<char>(ch));
    }
#else
    // Linux/macOS: выключаем эхо в терминале
    termios oldt{};
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        // Фоллбек: обычный ввод
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

// Возвращает true при успешном входе и заполняет out_user
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
            std::string login, pass;
            std::cout << "Логин: ";
            std::getline(std::cin, login);
            pass = read_password("Пароль: ");


            std::string err;
            if (!auth.login_user(login, pass, out_user, err)) {
                std::cout << "Ошибка: " << err << "\n\n";
                continue;
            }
            return true;
        }

        if (choice == 2) {
            std::string login, email, pass;
            std::cout << "Новый логин: ";
            std::getline(std::cin, login);
            std::cout << "Email: ";
            std::getline(std::cin, email);
            pass = read_password("Пароль: ");


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

            std::cout << "[Пункт пока в разработке]\n\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}