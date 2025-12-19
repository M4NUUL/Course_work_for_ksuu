#pragma once
#include <string>

std::string random_salt_hex(std::size_t bytes = 16);
std::string sha256_hex(const std::string& data);
