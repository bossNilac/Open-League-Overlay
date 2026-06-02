#ifndef LOL_OVERLAY_OVERLAY_DATA_H
#define LOL_OVERLAY_OVERLAY_DATA_H

#include <json/json.h>

#include <string>
#include <vector>

struct RoleInventoryRow {
    std::string role;
    std::string blueChampion;
    std::string redChampion;
    std::string bluePlayerName;
    std::string redPlayerName;
    int blueInventoryGold = 0;
    int redInventoryGold = 0;
    int blueCreepScore = 0;
    int redCreepScore = 0;
    int blueVisionScore = 0;
    int redVisionScore = 0;
    int blueKills = 0;
    int blueDeaths = 0;
    int blueAssists = 0;
    int redKills = 0;
    int redDeaths = 0;
    int redAssists = 0;
};

struct LaneOverlayStats {
    bool visible = false;
    std::string myChampion;
    std::string myName;
    std::string myTeam;
    std::string myPosition;
    int myCurrentGold = 0;
    int myInventoryGold = 0;
    int myCreepScore = 0;
    int myVisionScore = 0;
    double myCsPerMinute = 0.0;
    int myKills = 0;
    int myDeaths = 0;
    int myAssists = 0;

    bool enemyKnown = false;
    std::string enemyChampion;
    std::string enemyPosition;
    int enemyInventoryGold = 0;
    int enemyCreepScore = 0;
    int enemyVisionScore = 0;
    double enemyCsPerMinute = 0.0;
    int enemyKills = 0;
    int enemyDeaths = 0;
    int enemyAssists = 0;

    int blueInventoryGold = 0;
    int redInventoryGold = 0;
    int blueCreepScore = 0;
    int redCreepScore = 0;
    int blueVisionScore = 0;
    int redVisionScore = 0;
    int blueKills = 0;
    int blueDeaths = 0;
    int blueAssists = 0;
    int redKills = 0;
    int redDeaths = 0;
    int redAssists = 0;
    std::vector<RoleInventoryRow> roleInventoryRows;
};

bool operator==(const LaneOverlayStats& left, const LaneOverlayStats& right);
bool operator!=(const LaneOverlayStats& left, const LaneOverlayStats& right);

bool buildLaneOverlayStats(const Json::Value& root, LaneOverlayStats& stats);

#endif // LOL_OVERLAY_OVERLAY_DATA_H
