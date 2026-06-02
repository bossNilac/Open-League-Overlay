#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "lcu_client.h"

#include <windows.h>

#include "api/json_parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return L"";
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring envPath(const wchar_t* name) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return L"";
    }
    return buffer;
}

std::vector<std::filesystem::path> commonLockfilePaths() {
    std::vector<std::filesystem::path> paths;
    paths.emplace_back(L"C:\\Riot Games\\League of Legends\\lockfile");
    paths.emplace_back(L"C:\\Program Files\\Riot Games\\League of Legends\\lockfile");
    paths.emplace_back(L"C:\\Program Files (x86)\\Riot Games\\League of Legends\\lockfile");

    const std::wstring localAppData = envPath(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        paths.emplace_back(std::filesystem::path(localAppData) / L"Riot Games" / L"League of Legends" / L"lockfile");
    }
    return paths;
}

void appendUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    const auto normalized = path.lexically_normal();
    const auto exists = std::any_of(paths.begin(), paths.end(), [&](const std::filesystem::path& current) {
        return _wcsicmp(current.wstring().c_str(), normalized.wstring().c_str()) == 0;
    });
    if (!exists) {
        paths.push_back(normalized);
    }
}

void collectLeagueInstallPaths(const Json::Value& value, std::vector<std::filesystem::path>& paths) {
    if (value.isString()) {
        std::string text = value.asString();
        std::replace(text.begin(), text.end(), '/', '\\');
        const std::wstring wide = utf8ToWide(text);
        if (wide.find(L"League of Legends") != std::wstring::npos) {
            appendUniquePath(paths, std::filesystem::path(wide) / L"lockfile");
        }
        return;
    }

    if (value.isObject()) {
        for (const std::string& name : value.getMemberNames()) {
            std::string key = name;
            std::replace(key.begin(), key.end(), '/', '\\');
            const std::wstring wideKey = utf8ToWide(key);
            if (wideKey.find(L"League of Legends") != std::wstring::npos) {
                appendUniquePath(paths, std::filesystem::path(wideKey) / L"lockfile");
            }
            collectLeagueInstallPaths(value[name], paths);
        }
        return;
    }

    if (value.isArray()) {
        for (const Json::Value& child : value) {
            collectLeagueInstallPaths(child, paths);
        }
    }
}

std::vector<std::filesystem::path> riotInstallLockfilePaths() {
    std::vector<std::filesystem::path> paths;
    const std::wstring programData = envPath(L"PROGRAMDATA");
    if (programData.empty()) {
        return paths;
    }

    const std::filesystem::path installsPath =
        std::filesystem::path(programData) / L"Riot Games" / L"RiotClientInstalls.json";
    std::ifstream input(installsPath);
    if (!input) {
        return paths;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    Json::Value root;
    std::string error;
    if (parseApiJson(buffer.str(), root, error)) {
        collectLeagueInstallPaths(root, paths);
    }
    return paths;
}

bool readLockfile(const std::filesystem::path& path, LcuConnection& connection) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    std::string text;
    std::getline(file, text);
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ':')) {
        parts.push_back(part);
    }

    if (parts.size() < 5) {
        return false;
    }

    connection.ok = true;
    connection.baseUrl = parts[4] + "://127.0.0.1:" + parts[2];
    connection.userPassword = "riot:" + parts[3];
    connection.error.clear();
    return true;
}

std::string endpointUrl(const LcuConnection& connection, std::string path) {
    if (path.empty() || path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    return connection.baseUrl + path;
}
}

namespace LcuClient {
LcuConnection discover() {
    LcuConnection connection;
    std::vector<std::filesystem::path> paths = riotInstallLockfilePaths();
    const std::vector<std::filesystem::path> commonPaths = commonLockfilePaths();
    for (const std::filesystem::path& path : commonPaths) {
        appendUniquePath(paths, path);
    }

    for (const std::filesystem::path& path : paths) {
        if (readLockfile(path, connection)) {
            return connection;
        }
    }

    connection.error = "League Client lockfile not found";
    return connection;
}

HttpResponse get(const LcuConnection& connection, const std::string& path) {
    if (!connection.ok) {
        return HttpResponse{false, 0, "", connection.error};
    }
    return ApiCaller::getInstance()->getWithAuth(endpointUrl(connection, path), connection.userPassword, true);
}

HttpResponse postJson(const LcuConnection& connection, const std::string& path, const std::string& body) {
    if (!connection.ok) {
        return HttpResponse{false, 0, "", connection.error};
    }
    return ApiCaller::getInstance()->postJsonWithAuth(endpointUrl(connection, path), body, connection.userPassword, true);
}

HttpResponse putJson(const LcuConnection& connection, const std::string& path, const std::string& body) {
    if (!connection.ok) {
        return HttpResponse{false, 0, "", connection.error};
    }
    return ApiCaller::getInstance()->putJsonWithAuth(endpointUrl(connection, path), body, connection.userPassword, true);
}

HttpResponse deletePath(const LcuConnection& connection, const std::string& path) {
    if (!connection.ok) {
        return HttpResponse{false, 0, "", connection.error};
    }
    return ApiCaller::getInstance()->deleteWithAuth(endpointUrl(connection, path), connection.userPassword, true);
}
}
