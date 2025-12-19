#include "downloader.hpp"

#include <curl/curl.h>
#include <cstdio>
#include <string>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* stream) {
    return std::fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

bool download_https(const std::string& url,
                    const std::string& filepath,
                    std::string& error)
{
    error.clear();

    curl_global_init(CURL_GLOBAL_DEFAULT);

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        error = "cannot open file for write: " + filepath;
        return false;
    }

    // --- Basic ---
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    // Некоторые сайты ведут себя лучше с User-Agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "bdu-cli/1.0 (libcurl)");

    // --- Timeouts / anti-hang ---
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L); // 10 минут на весь файл

    // если скорость < 1KB/s 30 секунд подряд — считаем зависанием
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    // Иногда в сетях IPv6 ломает TLS/route — фиксируем на IPv4
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    // --- TLS verification (безопасно) ---
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

#ifdef __linux__
    // Debian/Ubuntu CA bundle и директория сертификатов
    curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl, CURLOPT_CAPATH, "/etc/ssl/certs");
#endif

    // Иногда помогает в “странных” сетях
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    // --- Debug (временно включи, если надо) ---
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = CURLE_OK;

    // --- Retry (3 попытки) ---
    for (int attempt = 1; attempt <= 3; ++attempt) {
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;

#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    std::fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error = curl_easy_strerror(res);
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        error = "HTTP code: " + std::to_string(http_code);
        return false;
    }

    return true;
}
