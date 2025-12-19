#include "crypto.hpp"
#include <openssl/sha.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <vector>

static std::string to_hex(const unsigned char* data, std::size_t len) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return oss.str();
}

std::string sha256_hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    return to_hex(hash, SHA256_DIGEST_LENGTH);
}

std::string random_salt_hex(std::size_t bytes) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<unsigned char> buf(bytes);
    for (std::size_t i = 0; i < bytes; ++i) buf[i] = static_cast<unsigned char>(dist(gen));
    return to_hex(buf.data(), buf.size());
}
