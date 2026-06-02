#ifndef LOL_OVERLAY_MATCH_HISTORY_H
#define LOL_OVERLAY_MATCH_HISTORY_H

#include <json/json.h>

#include <filesystem>
#include <string>
#include <vector>

struct MatchHistorySummary {
    std::filesystem::path path;
    std::string matchKey;
    std::string startedAtLocal;
    std::string endedAtLocal;
    std::string champion;
    std::string role;
    std::string enemyChampion;
    std::string laneResult;
    std::string grade;
    int performanceScore = 0;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int creepScore = 0;
    double csPerMinute = 0.0;
    int visionScore = 0;
    int inventoryGold = 0;
    int laneGoldDiff = 0;
    int laneCsDiff = 0;
    double durationSeconds = 0.0;
};

namespace MatchHistory {
std::vector<MatchHistorySummary> loadSummaries(const std::filesystem::path& performanceDir,
                                               const std::string& filterText);
std::vector<MatchHistorySummary> loadSummaryPage(const std::filesystem::path& performanceDir,
                                                 const std::string& filterText,
                                                 size_t offset,
                                                 size_t count,
                                                 bool& hasMore);
size_t countMatchFiles(const std::filesystem::path& performanceDir);
bool loadMatchFile(const std::filesystem::path& path, Json::Value& root);
Json::Value reportForMatch(Json::Value& root);
std::string formatDateTime(const std::string& timestamp);
std::string formatDuration(double seconds);
}

#endif // LOL_OVERLAY_MATCH_HISTORY_H
