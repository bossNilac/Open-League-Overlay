#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_paths.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace {
std::filesystem::path envPath(const wchar_t* name) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer);
}

void copyFileIfMissing(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code error;
    if (!std::filesystem::exists(source, error) || std::filesystem::exists(target, error)) {
        return;
    }
    std::filesystem::create_directories(target.parent_path(), error);
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::skip_existing, error);
}

void copyJsonDirectoryIfPresent(const std::filesystem::path& sourceDir, const std::filesystem::path& targetDir) {
    std::error_code error;
    if (!std::filesystem::exists(sourceDir, error) || !std::filesystem::is_directory(sourceDir, error)) {
        return;
    }
    std::filesystem::create_directories(targetDir, error);
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sourceDir, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        copyFileIfMissing(entry.path(), targetDir / entry.path().filename());
    }
}
}

namespace AppPaths {
std::string appName() {
    return "OpenLeagueOverlay";
}

std::filesystem::path moduleDirectory() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path localDataDirectory() {
    std::filesystem::path base = envPath(L"LOCALAPPDATA");
    if (base.empty()) {
        base = envPath(L"APPDATA");
    }
    if (base.empty()) {
        base = moduleDirectory();
    }
    return base / appName();
}

std::filesystem::path settingsPath() {
    return localDataDirectory() / "settings.json";
}

std::filesystem::path matchHistoryDirectory() {
    return localDataDirectory() / "match_history";
}

std::filesystem::path reportsDirectory() {
    return localDataDirectory() / "reports";
}

std::filesystem::path snapshotsDirectory() {
    return localDataDirectory() / "snapshots";
}

std::filesystem::path logsDirectory() {
    return localDataDirectory() / "logs";
}

std::filesystem::path cacheDirectory() {
    return localDataDirectory() / "cache";
}

std::filesystem::path configPath(const std::string& fileName) {
    return localDataDirectory() / fileName;
}

void ensureDataDirectories() {
    std::error_code error;
    std::filesystem::create_directories(localDataDirectory(), error);
    std::filesystem::create_directories(matchHistoryDirectory(), error);
    std::filesystem::create_directories(reportsDirectory(), error);
    std::filesystem::create_directories(snapshotsDirectory(), error);
    std::filesystem::create_directories(logsDirectory(), error);
    std::filesystem::create_directories(cacheDirectory(), error);
}

void migrateLegacyData(const std::filesystem::path& legacyDirectory) {
    ensureDataDirectories();
    copyJsonDirectoryIfPresent(legacyDirectory / "performance", matchHistoryDirectory());
    copyFileIfMissing(legacyDirectory / "overlay_config.ini", configPath("overlay_config.ini"));
    copyFileIfMissing(legacyDirectory / "scoreboard_config.ini", configPath("scoreboard_config.ini"));
}
}
