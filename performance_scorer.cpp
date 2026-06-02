#include "performance_scorer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace {
struct RoleBaseline {
    double expectedKda = 2.8;
    double expectedKp = 0.52;
    double expectedDeathsPer10 = 1.6;
    double expectedCsMin = 7.2;
    double expectedGoldShare = 0.22;
    double expectedVisionMin = 0.7;
};

std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

double asDouble(const Json::Value& value, const double fallback = 0.0) {
    return value.isNumeric() ? value.asDouble() : fallback;
}

double clampDouble(const double value, const double low, const double high) {
    return std::clamp(value, low, high);
}

std::string normalizedRole(std::string role) {
    std::transform(role.begin(), role.end(), role.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (role == "JUNGLE") return "JG";
    if (role == "MIDDLE") return "MID";
    if (role == "BOTTOM" || role == "ADC") return "BOT";
    if (role == "UTILITY" || role == "SUPPORT") return "SUP";
    return role;
}

RoleBaseline baselineForRole(const std::string& roleValue) {
    const std::string role = normalizedRole(roleValue);
    if (role == "TOP") return {2.4, 0.42, 1.7, 7.0, 0.21, 0.7};
    if (role == "JG") return {3.0, 0.58, 1.5, 5.8, 0.19, 0.9};
    if (role == "MID") return {2.8, 0.52, 1.6, 7.2, 0.22, 0.7};
    if (role == "BOT") return {2.7, 0.50, 1.7, 7.5, 0.24, 0.6};
    if (role == "SUP") return {3.2, 0.62, 1.9, 1.2, 0.13, 1.8};
    return {};
}

Json::Value stringArray(const std::vector<std::string>& values) {
    Json::Value array(Json::arrayValue);
    for (const std::string& value : values) {
        array.append(value);
    }
    return array;
}

const Json::Value& teamForPlayer(const Json::Value& teams, const std::string& teamName) {
    return teamName == "ORDER" ? teams["blue"] : teams["red"];
}

const Json::Value& rosterForPlayer(const Json::Value& players, const std::string& teamName) {
    return teamName == "ORDER" ? players["blue"] : players["red"];
}

int inventoryRankOnTeam(const Json::Value& me, const Json::Value& roster) {
    const int myGold = asInt(me["inventoryGold"]);
    int rank = 1;
    if (!roster.isArray()) {
        return rank;
    }
    for (const Json::Value& player : roster) {
        if (asInt(player["inventoryGold"]) > myGold) {
            ++rank;
        }
    }
    return rank;
}
}

namespace PerformanceScorer {
std::string gradeForScore(const int score) {
    if (score >= 97) return "S+";
    if (score >= 93) return "S";
    if (score >= 90) return "S-";
    if (score >= 86) return "A+";
    if (score >= 82) return "A";
    if (score >= 78) return "A-";
    if (score >= 74) return "B+";
    if (score >= 70) return "B";
    if (score >= 66) return "B-";
    if (score >= 62) return "C+";
    if (score >= 58) return "C";
    if (score >= 52) return "D";
    return "D-";
}

Json::Value score(const Json::Value& finalSnapshot) {
    const Json::Value& matchup = finalSnapshot["matchup"];
    const Json::Value& me = matchup["me"];
    const Json::Value& enemy = matchup["enemy"];
    const Json::Value& teams = finalSnapshot["teams"];
    const Json::Value& players = finalSnapshot["players"];
    const std::string role = normalizedRole(asString(matchup["role"], asString(me["position"], "MID")));
    const RoleBaseline baseline = baselineForRole(role);
    const bool support = role == "SUP";

    const int kills = asInt(me["kills"]);
    const int deaths = asInt(me["deaths"]);
    const int assists = asInt(me["assists"]);
    const int visionScore = asInt(me["visionScore"]);
    const int inventoryGold = asInt(me["inventoryGold"]);
    const double gameMinutes = std::max(1.0, asDouble(finalSnapshot["match"]["gameTimeSeconds"]) / 60.0);
    const double csPerMinute = asDouble(me["csPerMinute"]);
    const std::string myTeam = asString(me["team"]);
    const Json::Value& team = teamForPlayer(teams, myTeam);
    const int teamKills = std::max(1, asInt(team["kills"]));
    const int teamInventoryGold = std::max(1, asInt(team["inventoryGold"]));
    const double kdaRatio = (kills + assists) / static_cast<double>(std::max(1, deaths));
    const double kp = (kills + assists) / static_cast<double>(teamKills);
    const double deathsPer10 = deaths / std::max(1.0, gameMinutes / 10.0);
    const double visionPerMinute = visionScore / gameMinutes;

    double kdaScore = clampDouble((kdaRatio / baseline.expectedKda) * 12.0, 0.0, 18.0);
    double kpScore = clampDouble((kp / baseline.expectedKp) * 10.0, 0.0, 14.0);
    double deathScore = 14.0 * clampDouble(1.0 - (deathsPer10 / (baseline.expectedDeathsPer10 * 2.0)), 0.0, 1.0);
    if (deaths >= 10) {
        deathScore *= 0.65;
    } else if (deaths >= 8) {
        deathScore *= 0.80;
    } else if (deaths <= 2) {
        deathScore = std::min(14.0, deathScore + 2.0);
    }

    const double csScore = support
        ? clampDouble((visionPerMinute / 1.6) * 8.0 + (kp / baseline.expectedKp) * 4.0, 0.0, 16.0)
        : clampDouble((csPerMinute / baseline.expectedCsMin) * 12.0, 0.0, 16.0);

    const double playerGoldShare = inventoryGold / static_cast<double>(teamInventoryGold);
    double goldScore = clampDouble((playerGoldShare / baseline.expectedGoldShare) * 10.0, 0.0, 14.0);
    const int goldRank = inventoryRankOnTeam(me, rosterForPlayer(players, myTeam));
    if (goldRank == 1) {
        goldScore += 2.0;
    } else if (goldRank == 2) {
        goldScore += 1.0;
    }
    goldScore = clampDouble(goldScore, 0.0, 16.0);

    double visionScorePart = clampDouble((visionPerMinute / baseline.expectedVisionMin) * 7.0, 0.0, 10.0);
    if (role == "SUP" && visionPerMinute >= 2.0) {
        visionScorePart += 1.5;
    }
    if (role == "JG" && visionPerMinute >= 1.2) {
        visionScorePart += 1.0;
    }
    visionScorePart = clampDouble(visionScorePart, 0.0, 10.0);

    const int goldDiff = enemy.isObject() ? inventoryGold - asInt(enemy["inventoryGold"]) : 0;
    const int csDiff = enemy.isObject() ? asInt(me["creepScore"]) - asInt(enemy["creepScore"]) : 0;
    const int visionDiff = enemy.isObject() ? visionScore - asInt(enemy["visionScore"]) : 0;
    const int killDiff = enemy.isObject() ? kills - asInt(enemy["kills"]) : 0;
    const int deathDiff = enemy.isObject() ? asInt(enemy["deaths"]) - deaths : 0;
    const double goldDeltaScore = clampDouble(goldDiff / 4000.0, -1.0, 1.0) * 4.0;
    const double csDeltaScore = clampDouble(csDiff / 80.0, -1.0, 1.0) * 3.0;
    const double visionDeltaScore = clampDouble(visionDiff / 30.0, -1.0, 1.0) * 2.0;
    const double kdaDeltaScore = clampDouble((killDiff + deathDiff) / 8.0, -1.0, 1.0) * 3.0;
    double laneScore = 6.0 + goldDeltaScore + csDeltaScore + visionDeltaScore + kdaDeltaScore;
    laneScore = clampDouble(laneScore, 0.0, 12.0);

    double rawScore = kdaScore + kpScore + deathScore + csScore + goldScore + visionScorePart + laneScore;
    std::vector<std::string> capsApplied;
    const auto applyCap = [&](const double cap, const std::string& reason) {
        if (rawScore > cap) {
            rawScore = cap;
            capsApplied.push_back(reason);
        }
    };
    if (deaths >= 12) {
        applyCap(74.0, "12+ deaths cap");
    } else if (deaths >= 10) {
        applyCap(82.0, "10+ deaths cap");
    } else if (deaths >= 8 && kdaRatio < 3.0) {
        applyCap(86.0, "8+ deaths low KDA cap");
    }
    if (kp < baseline.expectedKp * 0.55) {
        applyCap(72.0, "low kill participation cap");
    }
    if (!support && csPerMinute < baseline.expectedCsMin * 0.55) {
        applyCap(76.0, "low CS/min cap");
    }
    if (laneScore <= 3.0 && !support) {
        applyCap(78.0, "hard lane loss cap");
    }

    std::vector<std::string> bonusesApplied;
    if (deaths <= 2 && kp >= baseline.expectedKp && csPerMinute >= baseline.expectedCsMin * 0.9) {
        rawScore += 3.0;
        bonusesApplied.push_back("excellent game bonus");
    }

    const double finalScore = clampDouble(rawScore, 0.0, 100.0);
    const int roundedScore = static_cast<int>(std::round(finalScore));

    Json::Value components(Json::objectValue);
    components["kdaScore"] = kdaScore;
    components["kpScore"] = kpScore;
    components["deathScore"] = deathScore;
    components["csScore"] = csScore;
    components["goldScore"] = goldScore;
    components["visionScore"] = visionScorePart;
    components["laneScore"] = laneScore;
    components["capsApplied"] = stringArray(capsApplied);
    components["bonusesApplied"] = stringArray(bonusesApplied);

    Json::Value result(Json::objectValue);
    result["score"] = roundedScore;
    result["rawScore"] = finalScore;
    result["grade"] = gradeForScore(roundedScore);
    result["label"] = "OL Score";
    result["components"] = components;
    result["kdaRatio"] = kdaRatio;
    result["killParticipation"] = kp;
    result["deathsPer10"] = deathsPer10;
    result["visionPerMinute"] = visionPerMinute;
    result["goldShare"] = playerGoldShare;
    result["inventoryRankOnTeam"] = goldRank;
    return result;
}
}
