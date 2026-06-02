#include "match_history.h"

#include "api/json_parser.h"
#include "performance_scorer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <system_error>

namespace {
std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

double asDouble(const Json::Value& value, const double fallback = 0.0) {
    return value.isNumeric() ? value.asDouble() : fallback;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

double impactEstimate(const Json::Value& player) {
    return asInt(player["kills"]) * 2.0 +
           asInt(player["assists"]) -
           asInt(player["deaths"]) * 2.0 +
           asDouble(player["csPerMinute"]) * 3.0 +
           asInt(player["visionScore"]) * 0.3 +
           asInt(player["inventoryGold"]) / 1000.0;
}

std::string gradeForScore(const int score) {
    if (score >= 95) return "S";
    if (score >= 90) return "A+";
    if (score >= 84) return "A-";
    if (score >= 78) return "B+";
    if (score >= 70) return "B";
    if (score >= 62) return "C+";
    if (score >= 52) return "C";
    if (score >= 42) return "D";
    return "F";
}

Json::Value teamDeltas(const Json::Value& teams) {
    Json::Value deltas(Json::objectValue);
    const Json::Value& blue = teams["blue"];
    const Json::Value& red = teams["red"];
    deltas["killsDiff"] = asInt(blue["kills"]) - asInt(red["kills"]);
    deltas["deathsDiff"] = asInt(blue["deaths"]) - asInt(red["deaths"]);
    deltas["assistsDiff"] = asInt(blue["assists"]) - asInt(red["assists"]);
    deltas["csDiff"] = asInt(blue["creepScore"]) - asInt(red["creepScore"]);
    deltas["visionDiff"] = asInt(blue["visionScore"]) - asInt(red["visionScore"]);
    deltas["inventoryGoldDiff"] = asInt(blue["inventoryGold"]) - asInt(red["inventoryGold"]);
    return deltas;
}

Json::Value flatPlayers(const Json::Value& players) {
    Json::Value flat(Json::arrayValue);
    for (const Json::Value& player : players["blue"]) {
        flat.append(player);
    }
    for (const Json::Value& player : players["red"]) {
        flat.append(player);
    }
    return flat;
}

Json::Value findExtremePlayer(const Json::Value& players, const std::string& team, const bool highest) {
    const Json::Value all = flatPlayers(players);
    bool found = false;
    double best = 0.0;
    Json::Value result(Json::nullValue);
    for (const Json::Value& player : all) {
        if (asString(player["team"]) != team) {
            continue;
        }
        const double score = impactEstimate(player);
        if (!found || (highest ? score > best : score < best)) {
            found = true;
            best = score;
            result = player;
            result["impactEstimate"] = score;
        }
    }
    return result;
}

Json::Value eventSummary(const Json::Value& events) {
    Json::Value summary(Json::objectValue);
    if (!events.isArray()) {
        return summary;
    }
    for (const Json::Value& event : events) {
        const std::string type = asString(event["type"]);
        summary[type] = asInt(summary[type]) + 1;
    }
    return summary;
}

Json::Value performanceObject(const Json::Value& me, const Json::Value& enemy, const Json::Value& teams) {
    const int kills = asInt(me["kills"]);
    const int deaths = asInt(me["deaths"]);
    const int assists = asInt(me["assists"]);
    const double csPerMinute = asDouble(me["csPerMinute"]);
    const int vision = asInt(me["visionScore"]);
    const int laneGoldDelta = enemy.isObject() ? asInt(me["inventoryGold"]) - asInt(enemy["inventoryGold"]) : 0;
    const std::string myTeam = asString(me["team"]);
    const Json::Value& team = myTeam == "ORDER" ? teams["blue"] : teams["red"];
    const int teamKills = std::max(1, asInt(team["kills"]));

    Json::Value components(Json::objectValue);
    components["kda"] = std::clamp(kills * 3 + assists * 2 - deaths * 3, -20, 30);
    components["cs"] = std::clamp(static_cast<int>(csPerMinute * 4.0), 0, 28);
    components["gold"] = std::clamp(asInt(me["inventoryGold"]) / 700, 0, 28);
    components["vision"] = std::clamp(vision / 2, 0, 16);
    components["deathsPenalty"] = -std::clamp(deaths * 2, 0, 24);
    components["laneDelta"] = std::clamp(laneGoldDelta / 300, -18, 18);
    components["teamContribution"] = std::clamp(static_cast<int>((kills + assists) * 20.0 / teamKills), 0, 18);

    int score = 45;
    for (const std::string& key : components.getMemberNames()) {
        score += asInt(components[key]);
    }
    score = std::clamp(score, 0, 100);

    Json::Value result(Json::objectValue);
    result["score"] = score;
    result["grade"] = gradeForScore(score);
    result["components"] = components;
    result["label"] = "Performance Estimate";
    return result;
}

std::string laneResult(const Json::Value& me, const Json::Value& enemy) {
    if (!enemy.isObject()) {
        return "Unknown";
    }
    const int gold = asInt(me["inventoryGold"]) - asInt(enemy["inventoryGold"]);
    const int cs = asInt(me["creepScore"]) - asInt(enemy["creepScore"]);
    if (gold > 800 || cs > 20) return "Won lane";
    if (gold < -800 || cs < -20) return "Lost lane";
    return "Even";
}

Json::Value generatedReport(const Json::Value& root) {
    const Json::Value& final = root["final"];
    const Json::Value& matchup = final["matchup"];
    const Json::Value& me = matchup["me"];
    const Json::Value& enemy = matchup["enemy"];
    const Json::Value& teams = final["teams"];
    const std::string myTeam = asString(me["team"]);
    const std::string enemyTeam = myTeam == "ORDER" ? "CHAOS" : "ORDER";

    Json::Value report(Json::objectValue);
    report["matchSummary"] = final["match"];
    report["playerSummary"] = me;
    report["laneMatchupSummary"] = matchup;
    report["roleDeltas"] = final["roleDeltas"];
    report["teamDeltas"] = teamDeltas(teams);
    report["objectiveEventSummary"] = eventSummary(root["events"]);
    report["biggestAllyCarry"] = findExtremePlayer(final["players"], myTeam, true);
    report["biggestEnemyThreat"] = findExtremePlayer(final["players"], enemyTeam, true);
    report["mostBehindAlly"] = findExtremePlayer(final["players"], myTeam, false);
    report["mostBehindEnemy"] = findExtremePlayer(final["players"], enemyTeam, false);
    report["performance"] = PerformanceScorer::score(final);
    report["summaryText"] = laneResult(me, enemy) + " based on inventory gold and CS at game end.";
    return report;
}

bool summaryMatchesFilter(const MatchHistorySummary& summary, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    const std::string haystack = lower(summary.champion + " " + summary.role + " " + summary.enemyChampion + " " +
                                      summary.grade + " " + summary.startedAtLocal + " " + summary.laneResult);
    return haystack.find(lower(filter)) != std::string::npos;
}

bool summaryFromRoot(const Json::Value& root, const std::filesystem::path& path, MatchHistorySummary& summary) {
    if (!root.isObject() || !root["final"].isObject()) {
        return false;
    }

    const Json::Value& final = root["final"];
    const Json::Value& me = final["matchup"]["me"];
    const Json::Value& enemy = final["matchup"]["enemy"];
    const Json::Value performance = PerformanceScorer::score(final);

    summary.path = path;
    summary.matchKey = asString(root["matchKey"], path.stem().string());
    summary.startedAtLocal = asString(root["startedAtLocal"]);
    summary.endedAtLocal = asString(root["endedAtLocal"]);
    summary.champion = asString(me["champion"], "-");
    summary.role = asString(final["matchup"]["role"], asString(me["position"], "?"));
    summary.enemyChampion = enemy.isObject() ? asString(enemy["champion"], "unknown") : "unknown";
    summary.grade = asString(performance["grade"], "Not scored");
    summary.performanceScore = asInt(performance["score"]);
    summary.kills = asInt(me["kills"]);
    summary.deaths = asInt(me["deaths"]);
    summary.assists = asInt(me["assists"]);
    summary.creepScore = asInt(me["creepScore"]);
    summary.csPerMinute = asDouble(me["csPerMinute"]);
    summary.visionScore = asInt(me["visionScore"]);
    summary.inventoryGold = asInt(me["inventoryGold"]);
    summary.durationSeconds = asDouble(final["match"]["gameTimeSeconds"]);
    summary.laneGoldDiff = enemy.isObject() ? asInt(me["inventoryGold"]) - asInt(enemy["inventoryGold"]) : 0;
    summary.laneCsDiff = enemy.isObject() ? asInt(me["creepScore"]) - asInt(enemy["creepScore"]) : 0;
    summary.laneResult = laneResult(me, enemy);
    return true;
}

std::string dedupeKey(const MatchHistorySummary& summary) {
    std::ostringstream key;
    key << summary.champion << '|'
        << summary.role << '|'
        << summary.enemyChampion << '|'
        << static_cast<int>(std::round(summary.durationSeconds)) << '|'
        << summary.kills << '/' << summary.deaths << '/' << summary.assists << '|'
        << summary.creepScore << '|'
        << summary.inventoryGold;
    return key.str();
}

std::vector<std::filesystem::directory_entry> sortedMatchFiles(const std::filesystem::path& performanceDir) {
    std::vector<std::filesystem::directory_entry> entries;
    std::error_code error;
    if (!std::filesystem::exists(performanceDir, error)) {
        return entries;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(performanceDir, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        entries.push_back(entry);
    }

    std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        std::error_code leftError;
        std::error_code rightError;
        const auto leftTime = left.last_write_time(leftError);
        const auto rightTime = right.last_write_time(rightError);
        if (!leftError && !rightError && leftTime != rightTime) {
            return leftTime > rightTime;
        }
        return left.path().filename().string() > right.path().filename().string();
    });
    return entries;
}
}

namespace MatchHistory {
bool loadMatchFile(const std::filesystem::path& path, Json::Value& root) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string parseError;
    return parseApiJson(buffer.str(), root, parseError) && root.isObject();
}

Json::Value reportForMatch(Json::Value& root) {
    if (root["final"].isObject()) {
        root["report"] = generatedReport(root);
    }
    if (!root["final"]["performance"].isObject() && root["report"]["performance"].isObject()) {
        root["final"]["performance"] = root["report"]["performance"];
    }
    if (!root["events"].isArray()) {
        root["events"] = Json::Value(Json::arrayValue);
    }
    if (!root["snapshots"].isArray()) {
        root["snapshots"] = Json::Value(Json::arrayValue);
    }
    return root["report"];
}

std::vector<MatchHistorySummary> loadSummaries(const std::filesystem::path& performanceDir,
                                               const std::string& filterText) {
    bool ignoredHasMore = false;
    return loadSummaryPage(performanceDir, filterText, 0, static_cast<size_t>(-1), ignoredHasMore);
}

std::vector<MatchHistorySummary> loadSummaryPage(const std::filesystem::path& performanceDir,
                                                 const std::string& filterText,
                                                 const size_t offset,
                                                 const size_t count,
                                                 bool& hasMore) {
    hasMore = false;
    std::vector<MatchHistorySummary> summaries;
    if (count == 0) {
        return summaries;
    }

    const std::vector<std::filesystem::directory_entry> entries = sortedMatchFiles(performanceDir);
    std::set<std::string> seen;
    size_t matched = 0;
    const size_t targetEnd = count == static_cast<size_t>(-1) ? static_cast<size_t>(-1) : offset + count;

    for (const std::filesystem::directory_entry& entry : entries) {
        Json::Value root;
        MatchHistorySummary summary;
        if (loadMatchFile(entry.path(), root) && summaryFromRoot(root, entry.path(), summary) &&
            summaryMatchesFilter(summary, filterText)) {
            if (!seen.insert(dedupeKey(summary)).second) {
                continue;
            }
            if (matched >= offset && summaries.size() < count) {
                summaries.push_back(summary);
            } else if (matched >= targetEnd) {
                hasMore = true;
                break;
            }
            ++matched;
        }
    }

    std::stable_sort(summaries.begin(), summaries.end(), [](const MatchHistorySummary& left, const MatchHistorySummary& right) {
        return left.startedAtLocal > right.startedAtLocal;
    });

    if (!hasMore && count != static_cast<size_t>(-1)) {
        hasMore = matched > offset + summaries.size();
    }
    return summaries;
}

size_t countMatchFiles(const std::filesystem::path& performanceDir) {
    return sortedMatchFiles(performanceDir).size();
}

std::string formatDateTime(const std::string& timestamp) {
    if (timestamp.size() < 15) {
        return timestamp.empty() ? "-" : timestamp;
    }
    return timestamp.substr(0, 4) + "-" + timestamp.substr(4, 2) + "-" + timestamp.substr(6, 2) +
           " " + timestamp.substr(9, 2) + ":" + timestamp.substr(11, 2);
}

std::string formatDuration(const double seconds) {
    const int total = std::max(0, static_cast<int>(std::round(seconds)));
    std::ostringstream out;
    out << (total / 60) << ':' << std::setw(2) << std::setfill('0') << (total % 60);
    return out.str();
}
}
