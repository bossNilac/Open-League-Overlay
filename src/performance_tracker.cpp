#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "performance_tracker.h"

#include "app_paths.h"
#include "item_catalog.h"
#include "performance_scorer.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr int SnapshotIntervalSeconds = 60;

struct TrackerState {
    bool active = false;
    bool saved = false;
    bool ownsRecorder = false;
    HANDLE recorderMutex = nullptr;
    std::string matchKey;
    std::string recorderLockName;
    Json::Value report;
    Json::Value finalSnapshot;
    Json::Value snapshots = Json::Value(Json::arrayValue);
    Json::Value events = Json::Value(Json::arrayValue);
    std::set<std::string> seenEvents;
    double lastSnapshotTime = -9999.0;
    int unavailablePolls = 0;
};

TrackerState g_tracker;

std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

double asDouble(const Json::Value& value, const double fallback = 0.0) {
    return value.isNumeric() ? value.asDouble() : fallback;
}

std::string normalizeIdentity(const std::string& text) {
    std::string normalized;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

void addIdentity(std::vector<std::string>& values, const std::string& text) {
    const std::string normalized = normalizeIdentity(text);
    if (!normalized.empty()) {
        values.push_back(normalized);
    }
}

std::vector<std::string> activeIdentities(const Json::Value& activePlayer) {
    std::vector<std::string> values;
    addIdentity(values, asString(activePlayer["summonerName"]));
    addIdentity(values, asString(activePlayer["riotId"]));
    addIdentity(values, asString(activePlayer["riotIdGameName"]));

    const std::string gameName = asString(activePlayer["riotIdGameName"]);
    const std::string tagLine = asString(activePlayer["riotIdTagLine"]);
    if (!gameName.empty() && !tagLine.empty()) {
        addIdentity(values, gameName + tagLine);
        addIdentity(values, gameName + "#" + tagLine);
    }
    return values;
}

std::vector<std::string> playerIdentities(const Json::Value& player) {
    std::vector<std::string> values;
    addIdentity(values, asString(player["summonerName"]));
    addIdentity(values, asString(player["riotId"]));
    addIdentity(values, asString(player["riotIdGameName"]));

    const std::string gameName = asString(player["riotIdGameName"]);
    const std::string tagLine = asString(player["riotIdTagLine"]);
    if (!gameName.empty() && !tagLine.empty()) {
        addIdentity(values, gameName + tagLine);
        addIdentity(values, gameName + "#" + tagLine);
    }
    return values;
}

bool identityMatches(const Json::Value& player, const Json::Value& activePlayer) {
    const std::vector<std::string> active = activeIdentities(activePlayer);
    const std::vector<std::string> playerIds = playerIdentities(player);
    for (const std::string& left : active) {
        for (const std::string& right : playerIds) {
            if (left == right) {
                return true;
            }
        }
    }
    return false;
}

const Json::Value* findMyPlayer(const Json::Value& allPlayers, const Json::Value& activePlayer) {
    if (!allPlayers.isArray()) {
        return nullptr;
    }
    for (const Json::Value& player : allPlayers) {
        if (identityMatches(player, activePlayer)) {
            return &player;
        }
    }
    return nullptr;
}

const Json::Value* findEnemyLaner(const Json::Value& allPlayers, const Json::Value& myPlayer) {
    const std::string myTeam = asString(myPlayer["team"]);
    const std::string myPosition = asString(myPlayer["position"]);
    if (myTeam.empty() || myPosition.empty()) {
        return nullptr;
    }
    for (const Json::Value& player : allPlayers) {
        if (asString(player["team"]) != myTeam && asString(player["position"]) == myPosition) {
            return &player;
        }
    }
    return nullptr;
}

std::string playerDisplayName(const Json::Value& player) {
    const std::string riotId = asString(player["riotId"]);
    if (!riotId.empty()) {
        return riotId;
    }
    const std::string gameName = asString(player["riotIdGameName"]);
    const std::string tagLine = asString(player["riotIdTagLine"]);
    if (!gameName.empty() && !tagLine.empty()) {
        return gameName + "#" + tagLine;
    }
    return asString(player["summonerName"], "-");
}

std::string positionLabel(const std::string& position) {
    if (position == "TOP") return "TOP";
    if (position == "JUNGLE") return "JG";
    if (position == "MIDDLE" || position == "MID") return "MID";
    if (position == "BOTTOM" || position == "ADC") return "BOT";
    if (position == "UTILITY" || position == "SUPPORT") return "SUP";
    return position.empty() ? "?" : position;
}

bool hasGameEndEvent(const Json::Value& root) {
    const Json::Value& events = root["events"]["Events"];
    if (!events.isArray()) {
        return false;
    }
    for (const Json::Value& event : events) {
        if (asString(event["EventName"]) == "GameEnd") {
            return true;
        }
    }
    return false;
}

std::string timeDisplay(const double seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    std::ostringstream out;
    out << (total / 60) << ':' << std::setw(2) << std::setfill('0') << (total % 60);
    return out.str();
}

Json::Value scoreObject(const Json::Value& player) {
    const Json::Value& scores = player["scores"];
    Json::Value value(Json::objectValue);
    value["kills"] = asInt(scores["kills"]);
    value["deaths"] = asInt(scores["deaths"]);
    value["assists"] = asInt(scores["assists"]);
    value["creepScore"] = asInt(scores["creepScore"]);
    value["visionScore"] = asInt(scores["wardScore"]);
    return value;
}

Json::Value playerObject(const Json::Value& player, const double gameTime) {
    const Json::Value scores = scoreObject(player);
    const double minutes = std::max(gameTime / 60.0, 1.0 / 60.0);

    Json::Value value(Json::objectValue);
    value["name"] = playerDisplayName(player);
    value["champion"] = asString(player["championName"], "-");
    value["team"] = asString(player["team"]);
    value["position"] = positionLabel(asString(player["position"]));
    value["inventoryGold"] = ItemCatalog::inventoryValue(player["items"]);
    value["kills"] = scores["kills"];
    value["deaths"] = scores["deaths"];
    value["assists"] = scores["assists"];
    value["creepScore"] = scores["creepScore"];
    value["csPerMinute"] = asDouble(scores["creepScore"]) / minutes;
    value["visionScore"] = scores["visionScore"];
    return value;
}

Json::Value teamTotals(const Json::Value& allPlayers, const std::string& team) {
    Json::Value totals(Json::objectValue);
    int inventoryGold = 0;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int creepScore = 0;
    int visionScore = 0;

    for (const Json::Value& player : allPlayers) {
        if (asString(player["team"]) != team) {
            continue;
        }
        const Json::Value scores = scoreObject(player);
        inventoryGold += ItemCatalog::inventoryValue(player["items"]);
        kills += asInt(scores["kills"]);
        deaths += asInt(scores["deaths"]);
        assists += asInt(scores["assists"]);
        creepScore += asInt(scores["creepScore"]);
        visionScore += asInt(scores["visionScore"]);
    }

    totals["inventoryGold"] = inventoryGold;
    totals["kills"] = kills;
    totals["deaths"] = deaths;
    totals["assists"] = assists;
    totals["creepScore"] = creepScore;
    totals["visionScore"] = visionScore;
    return totals;
}

int positionRank(const std::string& position) {
    if (position == "TOP") return 0;
    if (position == "JUNGLE" || position == "JG") return 1;
    if (position == "MIDDLE" || position == "MID") return 2;
    if (position == "BOTTOM" || position == "ADC" || position == "BOT") return 3;
    if (position == "UTILITY" || position == "SUPPORT" || position == "SUP") return 4;
    return 5;
}

Json::Value teamPlayers(const Json::Value& allPlayers, const std::string& team, const double gameTime) {
    Json::Value players(Json::arrayValue);
    std::vector<const Json::Value*> teamPlayers;
    for (const Json::Value& player : allPlayers) {
        if (asString(player["team"]) == team) {
            teamPlayers.push_back(&player);
        }
    }
    std::stable_sort(teamPlayers.begin(), teamPlayers.end(), [](const Json::Value* left, const Json::Value* right) {
        return positionRank(asString((*left)["position"])) < positionRank(asString((*right)["position"]));
    });
    for (const Json::Value* player : teamPlayers) {
        players.append(playerObject(*player, gameTime));
    }
    return players;
}

const Json::Value* playerForRoleAndTeam(const Json::Value& allPlayers, const std::string& role, const std::string& team) {
    for (const Json::Value& player : allPlayers) {
        if (asString(player["team"]) == team && positionLabel(asString(player["position"])) == role) {
            return &player;
        }
    }
    return nullptr;
}

Json::Value roleDeltaObject(const Json::Value* blue, const Json::Value* red, const double gameTime) {
    Json::Value delta(Json::objectValue);
    if (!blue || !red) {
        delta["available"] = false;
        return delta;
    }

    const Json::Value bluePlayer = playerObject(*blue, gameTime);
    const Json::Value redPlayer = playerObject(*red, gameTime);
    delta["available"] = true;
    delta["blueChampion"] = bluePlayer["champion"];
    delta["redChampion"] = redPlayer["champion"];
    delta["inventoryGoldDiff"] = asInt(bluePlayer["inventoryGold"]) - asInt(redPlayer["inventoryGold"]);
    delta["csDiff"] = asInt(bluePlayer["creepScore"]) - asInt(redPlayer["creepScore"]);
    delta["killsDiff"] = asInt(bluePlayer["kills"]) - asInt(redPlayer["kills"]);
    delta["deathsDiff"] = asInt(bluePlayer["deaths"]) - asInt(redPlayer["deaths"]);
    delta["assistsDiff"] = asInt(bluePlayer["assists"]) - asInt(redPlayer["assists"]);
    delta["visionDiff"] = asInt(bluePlayer["visionScore"]) - asInt(redPlayer["visionScore"]);
    delta["csPerMinuteDiff"] = asDouble(bluePlayer["csPerMinute"]) - asDouble(redPlayer["csPerMinute"]);
    return delta;
}

Json::Value roleDeltas(const Json::Value& allPlayers, const double gameTime) {
    Json::Value deltas(Json::objectValue);
    const std::vector<std::string> roles = {"TOP", "JG", "MID", "BOT", "SUP"};
    for (const std::string& role : roles) {
        deltas[role] = roleDeltaObject(playerForRoleAndTeam(allPlayers, role, "ORDER"),
                                       playerForRoleAndTeam(allPlayers, role, "CHAOS"),
                                       gameTime);
    }
    return deltas;
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

std::string laneText(const Json::Value& matchup) {
    const Json::Value& me = matchup["me"];
    const Json::Value& enemy = matchup["enemy"];
    if (!enemy.isObject()) {
        return "Lane opponent unavailable.";
    }
    const int gold = asInt(me["inventoryGold"]) - asInt(enemy["inventoryGold"]);
    const int cs = asInt(me["creepScore"]) - asInt(enemy["creepScore"]);
    if (gold > 800 || cs > 20) {
        return "Won lane by inventory or CS pressure.";
    }
    if (gold < -800 || cs < -20) {
        return "Lost lane by inventory or CS pressure.";
    }
    return "Lane was close overall.";
}

Json::Value generatedReport(const Json::Value& final, const Json::Value& events) {
    Json::Value report(Json::objectValue);
    const Json::Value& matchup = final["matchup"];
    const Json::Value& me = matchup["me"];
    const Json::Value& enemy = matchup["enemy"];
    const Json::Value& teams = final["teams"];
    const std::string myTeam = asString(me["team"]);
    const std::string enemyTeam = myTeam == "ORDER" ? "CHAOS" : "ORDER";

    report["matchSummary"] = final["match"];
    report["playerSummary"] = me;
    report["laneMatchupSummary"] = matchup;
    report["roleDeltas"] = final["roleDeltas"];
    report["teamDeltas"] = teamDeltas(teams);
    report["objectiveEventSummary"] = eventSummary(events);
    report["biggestAllyCarry"] = findExtremePlayer(final["players"], myTeam, true);
    report["biggestEnemyThreat"] = findExtremePlayer(final["players"], enemyTeam, true);
    report["mostBehindAlly"] = findExtremePlayer(final["players"], myTeam, false);
    report["mostBehindEnemy"] = findExtremePlayer(final["players"], enemyTeam, false);
    report["performance"] = PerformanceScorer::score(final);
    report["summaryText"] = laneText(matchup);
    return report;
}

Json::Value buildSnapshot(const Json::Value& root, const Json::Value& myPlayer, const Json::Value* enemy,
                          const std::string& reason) {
    const Json::Value& gameData = root["gameData"];
    const Json::Value& allPlayers = root["allPlayers"];
    const double gameTime = asDouble(gameData["gameTime"]);

    Json::Value snapshot(Json::objectValue);
    snapshot["timeSeconds"] = gameTime;
    snapshot["gameTimeDisplay"] = timeDisplay(gameTime);
    snapshot["reason"] = reason;
    snapshot["match"]["gameMode"] = asString(gameData["gameMode"]);
    snapshot["match"]["gameTimeSeconds"] = gameTime;
    snapshot["match"]["mapName"] = asString(gameData["mapName"]);
    snapshot["match"]["mapNumber"] = asInt(gameData["mapNumber"]);
    snapshot["teams"]["blue"] = teamTotals(allPlayers, "ORDER");
    snapshot["teams"]["red"] = teamTotals(allPlayers, "CHAOS");
    snapshot["players"]["blue"] = teamPlayers(allPlayers, "ORDER", gameTime);
    snapshot["players"]["red"] = teamPlayers(allPlayers, "CHAOS", gameTime);
    snapshot["matchup"]["role"] = positionLabel(asString(myPlayer["position"]));
    snapshot["matchup"]["me"] = playerObject(myPlayer, gameTime);
    snapshot["matchup"]["enemy"] = enemy ? playerObject(*enemy, gameTime) : Json::Value(Json::nullValue);
    snapshot["roleDeltas"] = roleDeltas(allPlayers, gameTime);
    snapshot["performance"] = PerformanceScorer::score(snapshot);
    return snapshot;
}

std::string eventKey(const Json::Value& event) {
    std::ostringstream key;
    key << asString(event["EventName"]) << '|'
        << std::fixed << std::setprecision(1) << asDouble(event["EventTime"]) << '|'
        << asString(event["KillerName"]) << '|'
        << asString(event["VictimName"]) << '|'
        << asString(event["TurretKilled"]) << '|'
        << asString(event["DragonType"]);
    return key.str();
}

bool trackedEventType(const std::string& type) {
    static const std::set<std::string> tracked = {
        "GameStart", "MinionsSpawning", "ChampionKill", "Multikill", "Ace", "FirstBlood", "FirstBrick",
        "TurretKilled", "InhibKilled", "DragonKill", "HeraldKill", "BaronKill", "InhibRespawningSoon",
        "InhibRespawned", "GameEnd"
    };
    return tracked.count(type) > 0;
}

void collectEvents(const Json::Value& root) {
    const Json::Value& events = root["events"]["Events"];
    if (!events.isArray()) {
        return;
    }

    for (const Json::Value& source : events) {
        const std::string type = asString(source["EventName"]);
        if (!trackedEventType(type)) {
            continue;
        }
        const std::string key = eventKey(source);
        if (!g_tracker.seenEvents.insert(key).second) {
            continue;
        }

        const double eventTime = asDouble(source["EventTime"]);
        Json::Value event(Json::objectValue);
        event["timeSeconds"] = eventTime;
        event["timeDisplay"] = timeDisplay(eventTime);
        event["type"] = type;
        event["killerName"] = asString(source["KillerName"]);
        event["victimName"] = asString(source["VictimName"]);
        event["team"] = asString(source["KillerTeam"]);
        Json::Value details(Json::objectValue);
        details["assisters"] = source["Assisters"];
        details["stolen"] = source["Stolen"];
        details["dragonType"] = asString(source["DragonType"]);
        details["turretKilled"] = asString(source["TurretKilled"]);
        details["inhibKilled"] = asString(source["InhibKilled"]);
        details["killStreak"] = asInt(source["KillStreak"]);
        event["details"] = details;
        g_tracker.events.append(event);
    }
}

std::string sanitizeFilePart(std::string text) {
    for (char& ch : text) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    if (text.empty()) {
        return "match";
    }
    return text;
}

std::string currentTimestampForFile() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &nowTime);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

std::wstring wideFromUtf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    return wide;
}

std::string liveRecorderKeyFor(const Json::Value& root, const Json::Value& myPlayer) {
    const Json::Value& gameData = root["gameData"];
    std::ostringstream key;
    key << sanitizeFilePart(asString(myPlayer["riotId"], asString(myPlayer["summonerName"], "player")))
        << '-' << sanitizeFilePart(asString(myPlayer["championName"], "champion"))
        << '-' << sanitizeFilePart(asString(gameData["gameMode"], "mode"))
        << '-' << asInt(gameData["mapNumber"]);
    return key.str();
}

std::string recorderMutexNameFor(const std::string& liveKey) {
    std::string safe = sanitizeFilePart(liveKey);
    if (safe.size() > 180) {
        safe.resize(180);
    }
    return "Local\\LOLOverlayRecorder-" + safe;
}

void releaseRecorderLock() {
    if (g_tracker.recorderMutex) {
        if (g_tracker.ownsRecorder) {
            ReleaseMutex(g_tracker.recorderMutex);
        }
        CloseHandle(g_tracker.recorderMutex);
    }
    g_tracker.recorderMutex = nullptr;
    g_tracker.ownsRecorder = false;
    g_tracker.recorderLockName.clear();
}

void resetTracker() {
    releaseRecorderLock();
    g_tracker = TrackerState{};
}

bool acquireRecorderLock(const Json::Value& root, const Json::Value& myPlayer) {
    const std::string name = recorderMutexNameFor(liveRecorderKeyFor(root, myPlayer));
    HANDLE mutex = CreateMutexW(nullptr, TRUE, wideFromUtf8(name).c_str());
    if (!mutex) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return false;
    }
    g_tracker.recorderMutex = mutex;
    g_tracker.ownsRecorder = true;
    g_tracker.recorderLockName = name;
    return true;
}

std::string matchKeyFor(const Json::Value& root, const Json::Value& myPlayer) {
    const Json::Value& gameData = root["gameData"];
    std::ostringstream key;
    key << sanitizeFilePart(asString(myPlayer["riotId"], asString(myPlayer["summonerName"], "player")))
        << '-' << sanitizeFilePart(asString(myPlayer["championName"], "champion"))
        << '-' << sanitizeFilePart(asString(gameData["gameMode"], "mode"))
        << '-' << asInt(gameData["mapNumber"])
        << '-' << currentTimestampForFile();
    return key.str();
}

std::string finalMatchKey() {
    const Json::Value& match = g_tracker.finalSnapshot["match"];
    const Json::Value& me = g_tracker.finalSnapshot["matchup"]["me"];
    const int durationSeconds = static_cast<int>(std::round(asDouble(match["gameTimeSeconds"])));
    std::ostringstream key;
    key << sanitizeFilePart(asString(me["name"], "player"))
        << '-' << sanitizeFilePart(asString(me["champion"], "champion"))
        << '-' << sanitizeFilePart(asString(match["gameMode"], "mode"))
        << '-' << asInt(match["mapNumber"])
        << '-' << durationSeconds << "s";
    return key.str();
}

void updateFinalSnapshot(const Json::Value& root, const Json::Value& myPlayer, const Json::Value* enemy) {
    g_tracker.finalSnapshot = buildSnapshot(root, myPlayer, enemy, "final");
    g_tracker.finalSnapshot.removeMember("timeSeconds");
    g_tracker.finalSnapshot.removeMember("gameTimeDisplay");
    g_tracker.finalSnapshot.removeMember("reason");
}

void saveReport() {
    if (!g_tracker.active || g_tracker.saved || g_tracker.finalSnapshot.isNull()) {
        return;
    }

    g_tracker.report["schemaVersion"] = 4;
    g_tracker.matchKey = finalMatchKey();
    g_tracker.report["matchKey"] = g_tracker.matchKey;
    g_tracker.report["endedAtLocal"] = currentTimestampForFile();
    g_tracker.report["final"] = g_tracker.finalSnapshot;
    g_tracker.report["snapshots"] = g_tracker.snapshots;
    g_tracker.report["events"] = g_tracker.events;
    g_tracker.report["report"] = generatedReport(g_tracker.finalSnapshot, g_tracker.events);

    std::filesystem::path outputDir = AppPaths::matchHistoryDirectory();
    std::error_code error;
    std::filesystem::create_directories(outputDir, error);
    const std::filesystem::path outputPath = outputDir / (g_tracker.matchKey + ".json");

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream output(outputPath, std::ios::trunc);
    if (output) {
        output << Json::writeString(builder, g_tracker.report);
        g_tracker.saved = true;
    }
}

void maybeRecordSnapshot(const Json::Value& root, const Json::Value& myPlayer, const Json::Value* enemy,
                         const std::string& reason, const bool force) {
    const double gameTime = asDouble(root["gameData"]["gameTime"]);
    if (!force && gameTime - g_tracker.lastSnapshotTime < SnapshotIntervalSeconds) {
        return;
    }
    g_tracker.snapshots.append(buildSnapshot(root, myPlayer, enemy, reason));
    g_tracker.lastSnapshotTime = gameTime;
}
}

namespace PerformanceTracker {
void observeLiveGame(const Json::Value& root) {
    const Json::Value& gameData = root["gameData"];
    const Json::Value& activePlayer = root["activePlayer"];
    const Json::Value& allPlayers = root["allPlayers"];
    if (!root.isObject() || !gameData.isObject() || !gameData["gameTime"].isNumeric() ||
        !activePlayer.isObject() || !allPlayers.isArray() || allPlayers.empty()) {
        observeGameUnavailable();
        return;
    }

    const Json::Value* myPlayer = findMyPlayer(allPlayers, activePlayer);
    if (!myPlayer) {
        observeGameUnavailable();
        return;
    }

    const Json::Value* enemy = findEnemyLaner(allPlayers, *myPlayer);
    const bool gameEnded = hasGameEndEvent(root);
    g_tracker.unavailablePolls = 0;
    if (!g_tracker.active) {
        resetTracker();
        if (!acquireRecorderLock(root, *myPlayer)) {
            return;
        }
        g_tracker.active = true;
        g_tracker.matchKey = matchKeyFor(root, *myPlayer);
        g_tracker.report["startedAtLocal"] = currentTimestampForFile();
        g_tracker.report["matchKey"] = g_tracker.matchKey;
        collectEvents(root);
        maybeRecordSnapshot(root, *myPlayer, enemy, "start", true);
    }
    if (!g_tracker.ownsRecorder || g_tracker.saved) {
        return;
    }

    collectEvents(root);
    maybeRecordSnapshot(root, *myPlayer, enemy, gameEnded ? "game-end" : "interval", gameEnded);
    updateFinalSnapshot(root, *myPlayer, enemy);
    if (gameEnded) {
        saveReport();
    }
}

void observeGameUnavailable() {
    if (g_tracker.active && g_tracker.saved) {
        resetTracker();
        return;
    }
    if (!g_tracker.active || !g_tracker.ownsRecorder || g_tracker.finalSnapshot.isNull()) {
        return;
    }
    ++g_tracker.unavailablePolls;
    if (g_tracker.unavailablePolls >= 5) {
        g_tracker.report["endedUnexpectedly"] = true;
        saveReport();
        resetTracker();
    }
}
}
