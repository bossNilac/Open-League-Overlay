#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "lcu_client.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return "";
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "";
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
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

std::vector<std::filesystem::path> processLockfilePaths() {
    std::vector<std::filesystem::path> paths;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return paths;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        const std::wstring exe = entry.szExeFile;
        if (_wcsicmp(exe.c_str(), L"LeagueClient.exe") != 0 &&
            _wcsicmp(exe.c_str(), L"LeagueClientUx.exe") != 0 &&
            _wcsicmp(exe.c_str(), L"LeagueClientUxRender.exe") != 0) {
            continue;
        }

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) {
            continue;
        }

        wchar_t imagePath[MAX_PATH] = {};
        DWORD length = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, imagePath, &length)) {
            paths.emplace_back(std::filesystem::path(imagePath).parent_path() / L"lockfile");
        }
        CloseHandle(process);
    }

    CloseHandle(snapshot);
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
    std::vector<std::filesystem::path> paths = processLockfilePaths();
    const std::vector<std::filesystem::path> commonPaths = commonLockfilePaths();
    paths.insert(paths.end(), commonPaths.begin(), commonPaths.end());

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
