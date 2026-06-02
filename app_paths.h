#ifndef LOL_OVERLAY_APP_PATHS_H
#define LOL_OVERLAY_APP_PATHS_H

#include <filesystem>
#include <string>

namespace AppPaths {
std::string appName();
std::filesystem::path moduleDirectory();
std::filesystem::path localDataDirectory();
std::filesystem::path settingsPath();
std::filesystem::path matchHistoryDirectory();
std::filesystem::path reportsDirectory();
std::filesystem::path snapshotsDirectory();
std::filesystem::path logsDirectory();
std::filesystem::path cacheDirectory();
std::filesystem::path configPath(const std::string& fileName);
void ensureDataDirectories();
void migrateLegacyData(const std::filesystem::path& legacyDirectory);
}

#endif // LOL_OVERLAY_APP_PATHS_H
