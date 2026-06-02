#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_settings.h"

#include "api/json_parser.h"
#include "app_paths.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
constexpr wchar_t RunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValueName[] = L"OpenLeagueOverlay";

std::wstring modulePath() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return {};
    }
    return path;
}

std::wstring startupCommand() {
    const std::wstring path = modulePath();
    if (path.empty()) {
        return {};
    }
    return L"\"" + path + L"\" --scoreboard";
}

std::wstring readStartupValue() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t data[2048] = {};
    DWORD dataSize = sizeof(data);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, RunValueName, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(data), &dataSize);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }
    return data;
}
}

namespace AppSettingsStore {
AppSettings load() {
    AppSettings settings;
    std::ifstream input(AppPaths::settingsPath());
    if (!input) {
        return settings;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    Json::Value root;
    std::string error;
    if (!parseApiJson(buffer.str(), root, error) || !root.isObject()) {
        return settings;
    }
    settings.startWithWindows = root["startWithWindows"].isBool() && root["startWithWindows"].asBool();
    settings.firstRunStartupPromptShown =
        root["firstRunStartupPromptShown"].isBool() && root["firstRunStartupPromptShown"].asBool();
    return settings;
}

void save(const AppSettings& settings) {
    AppPaths::ensureDataDirectories();
    Json::Value root(Json::objectValue);
    root["startWithWindows"] = settings.startWithWindows;
    root["firstRunStartupPromptShown"] = settings.firstRunStartupPromptShown;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream output(AppPaths::settingsPath(), std::ios::trunc);
    if (output) {
        output << Json::writeString(builder, root);
    }
}

bool startupRegistryEnabled() {
    return readStartupValue() == startupCommand();
}

bool enableStartWithWindows() {
    const std::wstring command = startupCommand();
    if (command.empty()) {
        return false;
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, RunValueName, 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool disableStartWithWindows() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return true;
    }
    const LSTATUS status = RegDeleteValueW(key, RunValueName);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

void syncStartupRegistry(const AppSettings& settings) {
    if (settings.startWithWindows) {
        enableStartWithWindows();
    } else {
        disableStartWithWindows();
    }
}
}
