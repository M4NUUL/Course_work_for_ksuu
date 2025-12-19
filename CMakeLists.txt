cmake_minimum_required(VERSION 3.16)
project(bdu_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Чтобы работало и в релизе, и в дебаге
add_compile_options(-Wall -Wextra)

# --- Зависимости системы ---
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)

# libpqxx обычно находится через pkg-config
pkg_check_modules(PQXX REQUIRED libpqxx)

include_directories(
    ${PQXX_INCLUDE_DIRS}
)

link_directories(
    ${PQXX_LIBRARY_DIRS}
)

# --- OpenXLSX как submodule ---
# Предполагаем, что ты добавишь его в external/OpenXLSX
add_subdirectory(external/OpenXLSX)

# --- Твой исполняемый файл ---
add_executable(bdu_cli
    src/main.cpp
    src/db.cpp
    src/auth.cpp
    src/crypto.cpp
    src/threats.cpp
    src/search_session.cpp
    src/downloader.cpp
    src/xlsx_converter.cpp
)

target_include_directories(bdu_cli PRIVATE
    src
)

target_link_libraries(bdu_cli PRIVATE
    ${PQXX_LIBRARIES}
    OpenSSL::SSL OpenSSL::Crypto
    CURL::libcurl
    OpenXLSX
)

# Иногда нужно прокинуть флаги pkg-config
target_compile_options(bdu_cli PRIVATE ${PQXX_CFLAGS_OTHER})
