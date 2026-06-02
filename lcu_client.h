#ifndef LOL_OVERLAY_LCU_CLIENT_H
#define LOL_OVERLAY_LCU_CLIENT_H

#include "api/api_caller.h"

#include <string>

struct LcuConnection {
    bool ok = false;
    std::string baseUrl;
    std::string userPassword;
    std::string error;
};

namespace LcuClient {
LcuConnection discover();
HttpResponse get(const LcuConnection& connection, const std::string& path);
HttpResponse postJson(const LcuConnection& connection, const std::string& path, const std::string& body);
HttpResponse putJson(const LcuConnection& connection, const std::string& path, const std::string& body);
HttpResponse deletePath(const LcuConnection& connection, const std::string& path);
}

#endif // LOL_OVERLAY_LCU_CLIENT_H
