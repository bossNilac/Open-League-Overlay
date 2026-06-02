#ifndef LOL_OVERLAY_API_CALLER_H
#define LOL_OVERLAY_API_CALLER_H
#include <string>
#include <vector>

struct HttpResponse {
    bool ok = false;
    long statusCode = 0;
    std::string body;
    std::string error;
};

class ApiCaller {
private:
    ApiCaller();
    ~ApiCaller();

    ApiCaller(const ApiCaller&) = delete;
    ApiCaller& operator=(const ApiCaller&) = delete;

public:
    static ApiCaller* getInstance();

    static void showMessage();

    HttpResponse get(const std::string &url, bool ignoreSslErrors = false) const;
    HttpResponse postJson(const std::string& url, const std::string& body, bool ignoreSslErrors = false) const;
    HttpResponse getWithAuth(const std::string& url, const std::string& userPassword, bool ignoreSslErrors = false) const;
    HttpResponse postJsonWithAuth(const std::string& url, const std::string& body, const std::string& userPassword,
                                  bool ignoreSslErrors = false) const;
    HttpResponse putJsonWithAuth(const std::string& url, const std::string& body, const std::string& userPassword,
                                 bool ignoreSslErrors = false) const;
    HttpResponse deleteWithAuth(const std::string& url, const std::string& userPassword,
                                bool ignoreSslErrors = false) const;
    HttpResponse getLiveEndpoint(const std::string &endpoint) const;
    std::string callApi(const std::string &url);
};

#endif // LOL_OVERLAY_API_CALLER_H
