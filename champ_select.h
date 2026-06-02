#ifndef LOL_OVERLAY_CHAMP_SELECT_H
#define LOL_OVERLAY_CHAMP_SELECT_H

#include <string>
#include <vector>

struct EnemyChampionCounter {
    std::string championName;
    std::string relation;
    double myWinRate = -1.0;
    int games = 0;
};

struct ChampSelectState {
    bool visible = false;
    std::string status;
    std::string myChampionName;
    int myChampionId = 0;
    std::string myPosition;
    bool myChampionLocked = false;
    std::vector<EnemyChampionCounter> enemyCounters;
    std::vector<std::string> runeLines;
    std::string runeStatus;
    std::vector<std::string> itemSetLines;
    std::string itemSetStatus;
};

bool pollChampSelectState(ChampSelectState& state, bool autoImportRunes);

#endif // LOL_OVERLAY_CHAMP_SELECT_H
