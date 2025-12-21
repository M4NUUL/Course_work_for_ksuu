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

static void sleep_1s() {
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
}

bool download_https(const std::string& url,
                    const std::string& filepath,
                    std::string& error)
{
    error.clear();

    // curl_global_init лучше один раз в main(), но так тоже работает.
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

    // Некоторые сайты лучше отвечают с UA
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "bdu-cli/1.0 (libcurl)");

    // --- Timeouts / anti-hang ---
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);

    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    // IPv4 помогает если IPv6 кривой
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    // --- Proxy support (ВАЖНО) ---
    // Позволяет использовать HTTPS_PROXY/HTTP_PROXY из окружения.
    // В вузах/корп. сетях без этого часто DNS/SSL ломаются.
    curl_easy_setopt(curl, CURLOPT_PROXY, ""); // включаем env proxy

    // --- TLS verification (безопасно) ---
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Дадим curl самому найти CA (если в системе настроено)
    // И дополнительно попробуем типичные пути Debian/Ubuntu.
#ifdef __linux__
    curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl, CURLOPT_CAPATH, "/etc/ssl/certs");
#endif

    // Иногда помогает в странных сетях
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    // Если хочешь увидеть подробности: раскомментируй
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = CURLE_OK;
    long http_code = 0;

    // --- Retry (3 попытки) ---
    for (int attempt = 1; attempt <= 3; ++attempt) {
        res = curl_easy_perform(curl);

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (res == CURLE_OK && http_code >= 200 && http_code < 300) {
            break;
        }

        // если попытка не последняя — немного подождём
        if (attempt < 3) sleep_1s();
    }

    std::fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        error = std::string("curl: ") + curl_easy_strerror(res);
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        error = "HTTP code: " + std::to_string(http_code);
        return false;
    }

    return true;
}
