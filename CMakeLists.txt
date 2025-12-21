cmake_minimum_required(VERSION 3.16)
project(bdu_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# warnings (только для GNU/Clang)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()

# --- Зависимости системы ---
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)

# libpqxx обычно удобно через pkg-config
pkg_check_modules(PQXX REQUIRED libpqxx)

# --- OpenXLSX как submodule ---
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
  src/importer.cpp
)

target_include_directories(bdu_cli PRIVATE
  src
  ${PQXX_INCLUDE_DIRS}
)

# Если pkg-config прокидывает спец флаги (например -pthread)
target_compile_options(bdu_cli PRIVATE ${PQXX_CFLAGS_OTHER})

target_link_libraries(bdu_cli PRIVATE
  ${PQXX_LIBRARIES}
  PostgreSQL::PostgreSQL
  OpenSSL::Crypto
  CURL::libcurl
  OpenXLSX
)

# Иногда pkg-config требует и линкер-флаги отдельно (редко, но полезно)
target_link_options(bdu_cli PRIVATE ${PQXX_LDFLAGS_OTHER})
