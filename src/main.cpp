#include <iostream>
#include <string>
#include <thread>
#include <chrono>


void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void show_banner() {
    std::cout <<
        "========================================\n"
        "        БАЗА ДАННЫХ УГРОЗ ФСТЭК\n"
        "========================================\n\n";
}

void fake_loading() {
    std::cout << "Загрузка";
    for (int i = 0; i < 3; ++i) {
        std::cout << ".";
        std::cout.flush();
#ifdef _WIN32
        Sleep(400);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

#endif
    }
    std::cout << "\n";
}

void main_menu() {
    std::cout <<
        "1. Поиск угроз\n"
        "2. Управление данными\n"
        "3. Отчёты и экспорт\n"
        "4. Выход\n"
        "> ";
}

int main() {
    clear_screen();
    show_banner();
    fake_loading();
    clear_screen();

    std::cout << "Добро пожаловать, это консольное приложение\n"
                 "по работе с базой данных угроз ФСТЭК.\n\n";

    int choice = 0;
    while (true) {
        main_menu();
        std::cin >> choice;

        switch (choice) {
            case 1:
                std::cout << "[Поиск угроз — в разработке]\n\n";
                break;
            case 2:
                std::cout << "[Управление данными — в разработке]\n\n";
                break;
            case 3:
                std::cout << "[Отчёты и экспорт — в разработке]\n\n";
                break;
            case 4:
                std::cout << "Выход...\n";
                return 0;
            default:
                std::cout << "Неверный пункт меню\n\n";
        }
    }
}
