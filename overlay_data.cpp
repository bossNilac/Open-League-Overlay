#include "overlay_data.h"

#include "item_catalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace {
std::string asString(const Json::Value& value, const std::string& fallback = "") {
    if (value.isString()) {
        return value.asString();
    }
    return fallback;
}

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

double asDouble(const Json::Value& value, const double fallback = 0.0) {
    return value.isNumeric() ? value.asDouble() : fallback;
}

std::string normalizeIdentity(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

void addIdentity(std::vector<std::string>& identities, const std::string& value) {
    const std::string normalized = normalizeIdentity(value);
    if (!normalized.empty()) {
        identities.push_back(normalized);
    }
}

std::vector<std::string> activePlayerIdentities(const Json::Value& activePlayer) {
    std::vector<std::string> identities;
    addIdentity(identities, asString(activePlayer["summonerName"]));
    addIdentity(identities, asString(activePlayer["riotId"]));
    addIdentity(identities, asString(activePlayer["riotIdGameName"]));

    const std::string gameName = asString(activePlayer["riotIdGameName"]);
    const std::string tagLine = asString(activePlayer["riotIdTagLine"]);
    if (!gameName.empty() && !tagLine.empty()) {
        addIdentity(identities, gameName + tagLine);
        addIdentity(identities, gameName + "#" + tagLine);
    }

    return identities;
}

std::vector<std::string> playerIdentities(const Json::Value& player) {
    std::vector<std::string> identities;
    addIdentity(identities, asString(player["summonerName"]));
    addIdentity(identities, asString(player["riotId"]));
    addIdentity(identities, asString(player["riotIdGameName"]));

    const std::string gameName = asString(player["riotIdGameName"]);
    const std::string tagLine = asString(player["riotIdTagLine"]);
    if (!gameName.empty() && !tagLine.empty()) {
        addIdentity(identities, gameName + tagLine);
        addIdentity(identities, gameName + "#" + tagLine);
    }

    return identities;
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

bool identityMatches(const Json::Value& player, const Json::Value& activePlayer) {
    const std::vector<std::string> activeIds = activePlayerIdentities(activePlayer);
    const std::vector<std::string> playerIds = playerIdentities(player);

    for (const std::string& activeId : activeIds) {
        for (const std::string& playerId : playerIds) {
            if (activeId == playerId) {
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

    for (const auto& player : allPlayers) {
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

    for (const auto& player : allPlayers) {
        if (asString(player["team"]) != myTeam && asString(player["position"]) == myPosition) {
            return &player;
        }
    }

    return nullptr;
}

double gameMinutes(const double gameTime) {
    return std::max(gameTime / 60.0, 1.0 / 60.0);
}

std::string positionLabel(const std::string& position) {
    if (position == "TOP") return "TOP";
    if (position == "JUNGLE") return "JG";
    if (position == "MIDDLE" || position == "MID") return "MID";
    if (position == "BOTTOM" || position == "ADC") return "BOT";
    if (position == "UTILITY" || position == "SUPPORT") return "SUP";
    return position.empty() ? "?" : position;
}

int positionRank(const std::string& position) {
    if (position == "TOP") return 0;
    if (position == "JUNGLE") return 1;
    if (position == "MIDDLE" || position == "MID") return 2;
    if (position == "BOTTOM" || position == "ADC") return 3;
    if (position == "UTILITY" || position == "SUPPORT") return 4;
    return 5;
}

int inventoryValue(const Json::Value& items) {
    return ItemCatalog::inventoryValue(items);
}

void fillScores(const Json::Value& player, int& kills, int& deaths, int& assists, int& cs, int& vision) {
    const Json::Value& scores = player["scores"];
    kills = asInt(scores["kills"]);
    deaths = asInt(scores["deaths"]);
    assists = asInt(scores["assists"]);
    cs = asInt(scores["creepScore"]);
    vision = asInt(scores["wardScore"]);
}

bool hasGameEndEvent(const Json::Value& root) {
    const Json::Value& events = root["events"]["Events"];
    if (!events.isArray()) {
        return false;
    }

    for (const auto& event : events) {
        if (asString(event["EventName"]) == "GameEnd") {
            return true;
        }
    }

    return false;
}

void fillInventoryOverview(const Json::Value& allPlayers, LaneOverlayStats& stats) {
    std::vector<const Json::Value*> players;
    for (const auto& player : allPlayers) {
        const std::string team = asString(player["team"]);
        int kills = 0;
        int deaths = 0;
        int assists = 0;
        int cs = 0;
        int vision = 0;
        fillScores(player, kills, deaths, assists, cs, vision);

        if (team == "ORDER") {
            stats.blueInventoryGold += inventoryValue(player["items"]);
            stats.blueKills += kills;
            stats.blueDeaths += deaths;
            stats.blueAssists += assists;
            stats.blueCreepScore += cs;
            stats.blueVisionScore += vision;
        } else if (team == "CHAOS") {
            stats.redInventoryGold += inventoryValue(player["items"]);
            stats.redKills += kills;
            stats.redDeaths += deaths;
            stats.redAssists += assists;
            stats.redCreepScore += cs;
            stats.redVisionScore += vision;
        }
        players.push_back(&player);
    }

    std::stable_sort(players.begin(), players.end(), [](const Json::Value* left, const Json::Value* right) {
        const int leftRank = positionRank(asString((*left)["position"]));
        const int rightRank = positionRank(asString((*right)["position"]));
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        return asString((*left)["team"]) < asString((*right)["team"]);
    });

    const std::vector<std::string> positions = {"TOP", "JUNGLE", "MIDDLE", "BOTTOM", "UTILITY"};
    for (const std::string& position : positions) {
        RoleInventoryRow row;
        row.role = positionLabel(position);

        for (const Json::Value* player : players) {
            if (asString((*player)["position"]) != position) {
                continue;
            }

            const std::string team = asString((*player)["team"]);
            int kills = 0;
            int deaths = 0;
            int assists = 0;
            int cs = 0;
            int vision = 0;
            fillScores(*player, kills, deaths, assists, cs, vision);

            if (team == "ORDER") {
                row.blueChampion = asString((*player)["championName"], "-");
                row.bluePlayerName = playerDisplayName(*player);
                row.blueInventoryGold = inventoryValue((*player)["items"]);
                row.blueCreepScore = cs;
                row.blueVisionScore = vision;
                row.blueKills = kills;
                row.blueDeaths = deaths;
                row.blueAssists = assists;
            } else if (team == "CHAOS") {
                row.redChampion = asString((*player)["championName"], "-");
                row.redPlayerName = playerDisplayName(*player);
                row.redInventoryGold = inventoryValue((*player)["items"]);
                row.redCreepScore = cs;
                row.redVisionScore = vision;
                row.redKills = kills;
                row.redDeaths = deaths;
                row.redAssists = assists;
            }
        }

        if (!row.blueChampion.empty() || !row.redChampion.empty()) {
            stats.roleInventoryRows.push_back(row);
        }
    }
}
}

bool operator==(const LaneOverlayStats& left, const LaneOverlayStats& right) {
    return left.visible == right.visible &&
           left.myChampion == right.myChampion &&
           left.myName == right.myName &&
           left.myTeam == right.myTeam &&
           left.myPosition == right.myPosition &&
           left.myCurrentGold == right.myCurrentGold &&
           left.myInventoryGold == right.myInventoryGold &&
           left.myCreepScore == right.myCreepScore &&
           left.myVisionScore == right.myVisionScore &&
           std::abs(left.myCsPerMinute - right.myCsPerMinute) < 0.05 &&
           left.myKills == right.myKills &&
           left.myDeaths == right.myDeaths &&
           left.myAssists == right.myAssists &&
           left.enemyKnown == right.enemyKnown &&
           left.enemyChampion == right.enemyChampion &&
           left.enemyPosition == right.enemyPosition &&
           left.enemyInventoryGold == right.enemyInventoryGold &&
           left.enemyCreepScore == right.enemyCreepScore &&
           left.enemyVisionScore == right.enemyVisionScore &&
           std::abs(left.enemyCsPerMinute - right.enemyCsPerMinute) < 0.05 &&
           left.enemyKills == right.enemyKills &&
           left.enemyDeaths == right.enemyDeaths &&
           left.enemyAssists == right.enemyAssists &&
           left.blueInventoryGold == right.blueInventoryGold &&
           left.redInventoryGold == right.redInventoryGold &&
           left.blueCreepScore == right.blueCreepScore &&
           left.redCreepScore == right.redCreepScore &&
           left.blueVisionScore == right.blueVisionScore &&
           left.redVisionScore == right.redVisionScore &&
           left.blueKills == right.blueKills &&
           left.blueDeaths == right.blueDeaths &&
           left.blueAssists == right.blueAssists &&
           left.redKills == right.redKills &&
           left.redDeaths == right.redDeaths &&
           left.redAssists == right.redAssists &&
           left.roleInventoryRows.size() == right.roleInventoryRows.size() &&
           std::equal(left.roleInventoryRows.begin(), left.roleInventoryRows.end(), right.roleInventoryRows.begin(),
                      [](const RoleInventoryRow& a, const RoleInventoryRow& b) {
                          return a.role == b.role &&
                                 a.blueChampion == b.blueChampion &&
                                 a.redChampion == b.redChampion &&
                                 a.bluePlayerName == b.bluePlayerName &&
                                 a.redPlayerName == b.redPlayerName &&
                                 a.blueInventoryGold == b.blueInventoryGold &&
                                 a.redInventoryGold == b.redInventoryGold &&
                                 a.blueCreepScore == b.blueCreepScore &&
                                 a.redCreepScore == b.redCreepScore &&
                                 a.blueVisionScore == b.blueVisionScore &&
                                 a.redVisionScore == b.redVisionScore &&
                                 a.blueKills == b.blueKills &&
                                 a.blueDeaths == b.blueDeaths &&
                                 a.blueAssists == b.blueAssists &&
                                 a.redKills == b.redKills &&
                                 a.redDeaths == b.redDeaths &&
                                 a.redAssists == b.redAssists;
                      });
}

bool operator!=(const LaneOverlayStats& left, const LaneOverlayStats& right) {
    return !(left == right);
}

bool buildLaneOverlayStats(const Json::Value& root, LaneOverlayStats& stats) {
    stats = LaneOverlayStats{};

    const Json::Value& gameData = root["gameData"];
    const Json::Value& activePlayer = root["activePlayer"];
    const Json::Value& allPlayers = root["allPlayers"];
    if (!root.isObject() || !gameData.isObject() || !gameData["gameTime"].isNumeric() ||
        !activePlayer.isObject() || !allPlayers.isArray() || allPlayers.empty()) {
        return false;
    }
    if (hasGameEndEvent(root)) {
        return false;
    }

    const Json::Value* myPlayer = findMyPlayer(allPlayers, activePlayer);
    if (!myPlayer) {
        return false;
    }

    const double minutes = gameMinutes(asDouble(gameData["gameTime"]));
    stats.visible = true;
    stats.myName = asString((*myPlayer)["riotId"], asString((*myPlayer)["summonerName"]));
    stats.myChampion = asString((*myPlayer)["championName"], "You");
    stats.myTeam = asString((*myPlayer)["team"]);
    stats.myPosition = positionLabel(asString((*myPlayer)["position"]));
    stats.myCurrentGold = asInt(activePlayer["currentGold"]);
    stats.myInventoryGold = inventoryValue((*myPlayer)["items"]);
    fillScores(*myPlayer, stats.myKills, stats.myDeaths, stats.myAssists, stats.myCreepScore, stats.myVisionScore);
    stats.myCsPerMinute = stats.myCreepScore / minutes;
    fillInventoryOverview(allPlayers, stats);

    const Json::Value* enemy = findEnemyLaner(allPlayers, *myPlayer);
    if (!enemy) {
        return true;
    }

    stats.enemyKnown = true;
    stats.enemyChampion = asString((*enemy)["championName"], "Enemy");
    stats.enemyPosition = positionLabel(asString((*enemy)["position"]));
    fillScores(*enemy, stats.enemyKills, stats.enemyDeaths, stats.enemyAssists, stats.enemyCreepScore, stats.enemyVisionScore);
    stats.enemyCsPerMinute = stats.enemyCreepScore / minutes;
    stats.enemyInventoryGold = inventoryValue((*enemy)["items"]);
    return true;
}
