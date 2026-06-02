#include "api_caller.h"
#include <curl/curl.h>
#include <iostream>
#include <vector>

namespace {
constexpr const char* LiveClientBase = "https://127.0.0.1:2999/liveclientdata/";

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* response = static_cast<std::string*>(userp);
    const size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string liveClientUrl(const std::string& endpoint) {
    if (endpoint.rfind("http://", 0) == 0 || endpoint.rfind("https://", 0) == 0) {
        return endpoint;
    }

    std::string normalized = endpoint;
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }

    return std::string(LiveClientBase) + normalized;
}

HttpResponse requestJson(const std::string& method, const std::string& url, const std::string& body,
                         const std::string& userPassword, const bool ignoreSslErrors,
                         const std::vector<std::string>& extraHeaders) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Unable to initialize curl";
        return response;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_slist* headers = nullptr;
    for (const std::string& header : extraHeaders) {
        headers = curl_slist_append(headers, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1200L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 4000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LOL-overlay/0.1");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (!body.empty() || method == "POST" || method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    if (!userPassword.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, userPassword.c_str());
    }
    if (ignoreSslErrors) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        response.error = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(result);
        return response;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
        response.error = "HTTP " + std::to_string(response.statusCode);
        return response;
    }

    response.ok = true;
    return response;
}
}

ApiCaller::ApiCaller() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

ApiCaller::~ApiCaller() {
    curl_global_cleanup();
}

ApiCaller* ApiCaller::getInstance() {
    static ApiCaller instance;
    return &instance;
}

void ApiCaller::showMessage() {
    if (getInstance()) std::cout << getInstance() << std::endl;
    else std::cout << "Null" << std::endl;
}

HttpResponse ApiCaller::get(const std::string &url, bool ignoreSslErrors) const {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Unable to initialize curl";
        return response;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LOL-overlay/0.1");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    if (ignoreSslErrors) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        response.error = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(result);
        return response;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
        response.error = "HTTP " + std::to_string(response.statusCode);
        return response;
    }

    response.ok = true;
    return response;
}

HttpResponse ApiCaller::postJson(const std::string& url, const std::string& body, const bool ignoreSslErrors) const {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Unable to initialize curl";
        return response;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1200L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 4000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LOL-overlay/0.1");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    if (ignoreSslErrors) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        response.error = errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(result);
        return response;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
        response.error = "HTTP " + std::to_string(response.statusCode);
        return response;
    }

    response.ok = true;
    return response;
}

HttpResponse ApiCaller::getWithAuth(const std::string& url, const std::string& userPassword,
                                    const bool ignoreSslErrors) const {
    return requestJson("GET", url, "", userPassword, ignoreSslErrors,
                       {"Accept: application/json"});
}

HttpResponse ApiCaller::postJsonWithAuth(const std::string& url, const std::string& body,
                                         const std::string& userPassword, const bool ignoreSslErrors) const {
    return requestJson("POST", url, body, userPassword, ignoreSslErrors,
                       {"Content-Type: application/json", "Accept: application/json"});
}

HttpResponse ApiCaller::putJsonWithAuth(const std::string& url, const std::string& body,
                                        const std::string& userPassword, const bool ignoreSslErrors) const {
    return requestJson("PUT", url, body, userPassword, ignoreSslErrors,
                       {"Content-Type: application/json", "Accept: application/json"});
}

HttpResponse ApiCaller::deleteWithAuth(const std::string& url, const std::string& userPassword,
                                       const bool ignoreSslErrors) const {
    return requestJson("DELETE", url, "", userPassword, ignoreSslErrors,
                       {"Accept: application/json"});
}

HttpResponse ApiCaller::getLiveEndpoint(const std::string &endpoint) const {
    return get(liveClientUrl(endpoint), true);
}

std::string ApiCaller::callApi(const std::string &url) {
    return getLiveEndpoint(url).body;
}

