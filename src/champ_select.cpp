#include "champ_select.h"

#include "api/api_caller.h"
#include "api/json_parser.h"
#include "champion_catalog.h"
#include "lcu_client.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct RuneRecommendation {
    bool ok = false;
    int primaryStyleId = 0;
    int subStyleId = 0;
    std::vector<int> selectedPerkIds;
    std::vector<std::string> lines;
    std::string source;
};

struct ItemSetRecommendation {
    bool ok = false;
    std::vector<int> coreItemIds;
    std::vector<int> bootItemIds;
    std::vector<std::string> lines;
    std::string source;
};

struct CounterRecord {
    std::string championName;
    std::string position;
    std::string relation;
    double myWinRate = -1.0;
    int games = 0;
};

struct MatchupGuideRecommendation {
    RuneRecommendation runes;
    ItemSetRecommendation items;
    CounterRecord counter;
    bool counterOk = false;
};

struct AnalysisCache {
    std::string key;
    std::vector<CounterRecord> counters;
    RuneRecommendation genericRunes;
    ItemSetRecommendation genericItems;
    std::chrono::steady_clock::time_point fetchedAt{};
};

struct MatchupGuideCacheEntry {
    std::string key;
    MatchupGuideRecommendation guide;
    std::chrono::steady_clock::time_point fetchedAt{};
};

AnalysisCache analysisCache;
std::vector<MatchupGuideCacheEntry> matchupGuideCache;
std::string importedRuneKey;
std::string importedItemSetKey;

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream out;
    for (const unsigned char ch : text) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                        << std::dec << std::setfill(' ');
                } else {
                    out << ch;
                }
        }
    }
    return out.str();
}

std::string roleToOpgg(std::string role) {
    std::transform(role.begin(), role.end(), role.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (role == "middle" || role == "mid") return "mid";
    if (role == "jungle" || role == "jg") return "jungle";
    if (role == "bottom" || role == "bot" || role == "adc") return "adc";
    if (role == "utility" || role == "support" || role == "sup") return "support";
    if (role == "top") return "top";
    return "all";
}

std::string roleLabel(const std::string& role) {
    const std::string opgg = roleToOpgg(role);
    if (opgg == "jungle") return "JG";
    if (opgg == "mid") return "MID";
    if (opgg == "adc") return "BOT";
    if (opgg == "support") return "SUP";
    if (opgg == "top") return "TOP";
    return "ANY";
}

std::string firstTextContent(const Json::Value& root) {
    return root["result"]["content"][0]["text"].isString()
        ? root["result"]["content"][0]["text"].asString()
        : "";
}

std::string relationForWinRate(const double winRate) {
    if (winRate >= 52.0) {
        return "favored";
    }
    if (winRate <= 48.0) {
        return "hard";
    }
    return "even";
}

std::string postMcpTool(const std::string& name, const std::string& argumentsJson) {
    std::ostringstream body;
    body << R"({"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":")"
         << jsonEscape(name) << R"(","arguments":)" << argumentsJson << R"(}})";

    const HttpResponse response = ApiCaller::getInstance()->postJson("https://mcp-api.op.gg/mcp", body.str());
    if (!response.ok) {
        return "";
    }

    Json::Value root;
    std::string parseError;
    if (!parseApiJson(response.body, root, parseError) || root["error"].isObject()) {
        return "";
    }
    return firstTextContent(root);
}

std::vector<int> jsonIntArray(const Json::Value& value) {
    std::vector<int> result;
    if (!value.isArray()) {
        return result;
    }
    for (const Json::Value& item : value) {
        if (item.isNumeric()) {
            result.push_back(item.asInt());
        }
    }
    return result;
}

void addUniqueItemId(std::vector<int>& result, const int itemId, const size_t maxItems) {
    if (itemId <= 0 || result.size() >= maxItems) {
        return;
    }
    if (std::find(result.begin(), result.end(), itemId) == result.end()) {
        result.push_back(itemId);
    }
}

std::vector<int> jsonItemIdArray(const Json::Value& value, const size_t maxItems) {
    std::vector<int> result;
    if (!value.isArray()) {
        return result;
    }
    for (const Json::Value& item : value) {
        if (item.isNumeric()) {
            addUniqueItemId(result, item.asInt(), maxItems);
        } else if (item.isString()) {
            try {
                addUniqueItemId(result, std::stoi(item.asString()), maxItems);
            } catch (...) {
            }
        }
    }
    return result;
}

std::string joinItemIds(const std::vector<int>& ids) {
    if (ids.empty()) {
        return "-";
    }
    std::ostringstream out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << ids[i];
    }
    return out.str();
}

std::string joinNames(const Json::Value& value, const size_t maxItems) {
    if (!value.isArray()) {
        return "-";
    }
    std::string result;
    const Json::ArrayIndex count = std::min<Json::ArrayIndex>(value.size(), static_cast<Json::ArrayIndex>(maxItems));
    for (Json::ArrayIndex i = 0; i < count; ++i) {
        if (!result.empty()) {
            result += ", ";
        }
        result += value[i].asString();
    }
    return result.empty() ? "-" : result;
}

CounterRecord counterFromJsonGuide(const Json::Value& meta, const std::string& enemyChampion) {
    CounterRecord result;
    result.championName = enemyChampion;
    result.position = roleLabel(asString(meta["position"]));
    result.relation = "NA";

    const std::string enemyKey = ChampionCatalog::opggNameForChampion(enemyChampion);
    if (enemyKey.empty()) {
        return result;
    }

    const auto readCounters = [&](const Json::Value& counters) {
        if (!counters.isArray()) {
            return false;
        }
        for (const Json::Value& counter : counters) {
            const std::string counterName = asString(counter["champion_name"]);
            if (ChampionCatalog::opggNameForChampion(counterName) != enemyKey) {
                continue;
            }
            const int play = asInt(counter["play"]);
            const int win = asInt(counter["win"]);
            if (play <= 0) {
                return false;
            }
            const double winRate = (static_cast<double>(win) / static_cast<double>(play)) * 100.0;
            result.championName = counterName.empty() ? enemyChampion : counterName;
            if (result.position.empty() || result.position == "ANY") {
                result.position = roleLabel(asString(meta["position"]));
            }
            result.relation = relationForWinRate(winRate);
            result.myWinRate = winRate;
            result.games = play;
            return true;
        }
        return false;
    };

    if (readCounters(meta["data"]["counters"])) {
        return result;
    }

    const Json::Value& positions = meta["data"]["summary"]["positions"];
    if (positions.isArray()) {
        for (const Json::Value& position : positions) {
            if (readCounters(position["counters"])) {
                if (result.position.empty() || result.position == "ANY") {
                    result.position = roleLabel(asString(position["name"]));
                }
                return result;
            }
        }
    }

    const std::string laneAdvantage = asString(meta["data"]["lane_advantage_champion"]);
    const std::string style = asString(meta["data"]["recommended_play_style"]);
    if (!laneAdvantage.empty() && laneAdvantage != "null") {
        const std::string myChampion = asString(meta["my_champion"]);
        if (ChampionCatalog::opggNameForChampion(laneAdvantage) == ChampionCatalog::opggNameForChampion(myChampion)) {
            result.relation = "favored";
        } else if (laneAdvantage == "EVEN" || laneAdvantage == "even") {
            result.relation = "even";
        } else {
            result.relation = "hard";
        }
    } else if (!style.empty() && style != "null") {
        result.relation = style;
    }
    return result;
}

MatchupGuideRecommendation recommendationFromJsonGuide(const std::string& guideText,
                                                       const std::string& enemyChampion = "") {
    MatchupGuideRecommendation recommendation;
    Json::Value meta;
    std::string parseError;
    if (!parseApiJson(guideText, meta, parseError)) {
        return recommendation;
    }

    if (!enemyChampion.empty()) {
        recommendation.counter = counterFromJsonGuide(meta, enemyChampion);
        recommendation.counterOk = recommendation.counter.myWinRate >= 0.0;
    }

    const Json::Value& data = meta["data"];
    const Json::Value* runeBuild = nullptr;
    if (data["runes"].isArray() && !data["runes"].empty() && data["runes"][0].isObject()) {
        runeBuild = &data["runes"][0];
    } else if (data["rune_pages"].isArray() && !data["rune_pages"].empty()) {
        const Json::Value& builds = data["rune_pages"][0]["builds"];
        if (builds.isArray() && !builds.empty() && builds[0].isObject()) {
            runeBuild = &builds[0];
        }
    }

    if (runeBuild) {
        const Json::Value& runes = *runeBuild;
        recommendation.runes.primaryStyleId = asInt(runes["primary_page_id"]);
        recommendation.runes.subStyleId = asInt(runes["secondary_page_id"]);
        recommendation.runes.selectedPerkIds = jsonIntArray(runes["primary_rune_ids"]);
        const std::vector<int> secondary = jsonIntArray(runes["secondary_rune_ids"]);
        const std::vector<int> shards = jsonIntArray(runes["stat_mod_ids"]);
        recommendation.runes.selectedPerkIds.insert(recommendation.runes.selectedPerkIds.end(),
                                                    secondary.begin(), secondary.end());
        recommendation.runes.selectedPerkIds.insert(recommendation.runes.selectedPerkIds.end(),
                                                    shards.begin(), shards.end());
        recommendation.runes.ok = recommendation.runes.primaryStyleId > 0 && recommendation.runes.subStyleId > 0 &&
                                  recommendation.runes.selectedPerkIds.size() >= 9;
        recommendation.runes.source = "matchup";
        recommendation.runes.lines.push_back("Runes: " + asString(runes["primary_page_name"]) +
                                             " / " + asString(runes["secondary_page_name"]));
        recommendation.runes.lines.push_back("Primary: " + joinNames(runes["primary_rune_names"], 4));
        recommendation.runes.lines.push_back("Secondary: " + joinNames(runes["secondary_rune_names"], 3));
    }

    if (data["core_items"].isArray() && !data["core_items"].empty()) {
        recommendation.items.coreItemIds = jsonItemIdArray(data["core_items"][0]["ids"], 3);
    }
    if (data["boots"].isArray() && !data["boots"].empty()) {
        recommendation.items.bootItemIds = jsonItemIdArray(data["boots"][0]["ids"], 1);
    }
    recommendation.items.ok = !recommendation.items.coreItemIds.empty() || !recommendation.items.bootItemIds.empty();
    if (recommendation.items.ok) {
        recommendation.items.source = "matchup";
        recommendation.items.lines.push_back("Items: " + joinNames(data["core_items"][0]["ids_names"], 3));
        recommendation.items.lines.push_back("Boots: " + joinNames(data["boots"][0]["ids_names"], 1));
    }
    return recommendation;
}

std::vector<int> parseNumberList(const std::string& text) {
    std::vector<int> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        try {
            result.push_back(std::stoi(token));
        } catch (...) {
        }
    }
    return result;
}

RuneRecommendation parseGenericRunesFromAnalysis(const std::string& text) {
    RuneRecommendation recommendation;
    const std::regex runesPattern(
        R"(Runes\((\d+),[^,]*,[^,]*,(\d+),"[^"]*",\[([^\]]+)\],\[[^\]]*\],(\d+),"[^"]*",\[([^\]]+)\],\[[^\]]*\],\[([^\]]+)\])");
    std::smatch match;
    if (!std::regex_search(text, match, runesPattern)) {
        return recommendation;
    }

    recommendation.primaryStyleId = std::stoi(match[2].str());
    recommendation.subStyleId = std::stoi(match[4].str());
    recommendation.selectedPerkIds = parseNumberList(match[3].str());
    const std::vector<int> secondary = parseNumberList(match[5].str());
    const std::vector<int> shards = parseNumberList(match[6].str());
    recommendation.selectedPerkIds.insert(recommendation.selectedPerkIds.end(), secondary.begin(), secondary.end());
    recommendation.selectedPerkIds.insert(recommendation.selectedPerkIds.end(), shards.begin(), shards.end());
    recommendation.ok = recommendation.primaryStyleId > 0 && recommendation.subStyleId > 0 &&
                        recommendation.selectedPerkIds.size() >= 9;
    recommendation.source = "champion";
    recommendation.lines.push_back("Runes: OP.GG champion default");
    return recommendation;
}

ItemSetRecommendation parseGenericItemsFromAnalysis(const std::string& text) {
    ItemSetRecommendation recommendation;
    const std::regex bootsPattern(R"(Boots\(\[([^\]]*)\])");
    std::vector<std::vector<int>> itemGroups;
    for (std::sregex_iterator it(text.begin(), text.end(), bootsPattern), end; it != end; ++it) {
        itemGroups.push_back(parseNumberList((*it)[1].str()));
        if (itemGroups.size() >= 2) {
            break;
        }
    }
    if (itemGroups.empty()) {
        return recommendation;
    }

    recommendation.bootItemIds = itemGroups[0];
    if (itemGroups.size() > 1) {
        recommendation.coreItemIds = itemGroups[1];
    }
    if (recommendation.bootItemIds.size() > 1) {
        recommendation.bootItemIds.resize(1);
    }
    if (recommendation.coreItemIds.size() > 3) {
        recommendation.coreItemIds.resize(3);
    }
    recommendation.ok = !recommendation.coreItemIds.empty() || !recommendation.bootItemIds.empty();
    if (recommendation.ok) {
        recommendation.source = "champion";
        recommendation.lines.push_back("Items: " + joinItemIds(recommendation.coreItemIds));
        recommendation.lines.push_back("Boots: " + joinItemIds(recommendation.bootItemIds));
    }
    return recommendation;
}

std::vector<CounterRecord> parseCountersFromAnalysis(const std::string& text) {
    std::vector<CounterRecord> counters;
    const auto addOrUpdate = [&counters](CounterRecord record) {
        const std::string key = ChampionCatalog::opggNameForChampion(record.championName);
        for (CounterRecord& existing : counters) {
            if (ChampionCatalog::opggNameForChampion(existing.championName) == key) {
                existing = record;
                return;
            }
        }
        counters.push_back(std::move(record));
    };

    const std::regex rawCounterPattern(R"RAW((^|[^A-Za-z])Counter\((\d+),"([^"]+)",(\d+),(\d+)\))RAW");
    for (std::sregex_iterator it(text.begin(), text.end(), rawCounterPattern), end; it != end; ++it) {
        const std::smatch match = *it;
        const int play = std::stoi(match[4].str());
        const int win = std::stoi(match[5].str());
        const double winRate = play > 0 ? (static_cast<double>(win) / static_cast<double>(play)) * 100.0 : -1.0;
        addOrUpdate(CounterRecord{
            match[3].str(),
            "",
            winRate >= 0.0 ? relationForWinRate(winRate) : "NA",
            winRate,
            play
        });
    }
    return counters;
}

AnalysisCache fetchChampionAnalysis(const std::string& championName, const std::string& position) {
    const std::string champion = ChampionCatalog::opggNameForChampion(championName);
    const std::string opggPosition = roleToOpgg(position);
    const std::string key = champion + "|" + opggPosition;
    const auto now = std::chrono::steady_clock::now();
    if (analysisCache.key == key && now - analysisCache.fetchedAt < std::chrono::minutes(10)) {
        return analysisCache;
    }

    std::ostringstream args;
    args << R"({"game_mode":"ranked","champion":")" << jsonEscape(champion)
         << R"(","position":")" << jsonEscape(opggPosition == "all" ? "all" : opggPosition)
         << R"(","lang":"en_US","desired_output_fields":[)"
         << R"("data.summary.positions[].counters[].{champion_id,champion_name,play,win}",)"
         << R"("data.boots.{ids[],ids_names[]}",)"
         << R"("data.core_items.{ids[],ids_names[]}",)"
         << R"("data.runes.{id,pick_rate,play,primary_page_id,primary_page_name,primary_rune_ids[],primary_rune_names[],secondary_page_id,secondary_page_name,secondary_rune_ids[],secondary_rune_names[],stat_mod_ids[],stat_mod_names[],win}")"
         << R"(]})";

    const std::string text = postMcpTool("lol_get_champion_analysis", args.str());
    analysisCache.key = key;
    analysisCache.fetchedAt = now;
    analysisCache.counters = parseCountersFromAnalysis(text);
    analysisCache.genericRunes = parseGenericRunesFromAnalysis(text);
    analysisCache.genericItems = parseGenericItemsFromAnalysis(text);
    return analysisCache;
}

MatchupGuideRecommendation fetchMatchupGuide(const std::string& myChampion, const std::string& enemyChampion,
                                             const std::string& position) {
    if (myChampion.empty() || enemyChampion.empty()) {
        return {};
    }

    const std::string myKey = ChampionCatalog::opggNameForChampion(myChampion);
    const std::string enemyKey = ChampionCatalog::opggNameForChampion(enemyChampion);
    const std::string opggPosition = roleToOpgg(position);
    const std::string key = myKey + "|" + enemyKey + "|" + opggPosition;
    const auto now = std::chrono::steady_clock::now();
    for (const MatchupGuideCacheEntry& entry : matchupGuideCache) {
        if (entry.key == key && now - entry.fetchedAt < std::chrono::minutes(10)) {
            return entry.guide;
        }
    }

    std::ostringstream args;
    args << R"({"position":")" << jsonEscape(opggPosition)
         << R"(","my_champion":")" << jsonEscape(myKey)
         << R"(","opponent_champion":")" << jsonEscape(enemyKey)
         << R"(","lang":"en_US"})";
    const std::string text = postMcpTool("lol_get_lane_matchup_guide", args.str());
    MatchupGuideRecommendation guide = text.empty()
        ? MatchupGuideRecommendation{}
        : recommendationFromJsonGuide(text, enemyChampion);

    matchupGuideCache.erase(std::remove_if(matchupGuideCache.begin(), matchupGuideCache.end(),
                                           [&](const MatchupGuideCacheEntry& entry) {
                                               return entry.key == key ||
                                                      now - entry.fetchedAt >= std::chrono::minutes(10);
                                           }),
                            matchupGuideCache.end());
    matchupGuideCache.push_back(MatchupGuideCacheEntry{key, guide, now});
    if (matchupGuideCache.size() > 32) {
        matchupGuideCache.erase(matchupGuideCache.begin());
    }
    return guide;
}

Json::Value findParticipantByCell(const Json::Value& participants, const int cellId) {
    if (!participants.isArray()) {
        return {};
    }
    for (const Json::Value& participant : participants) {
        if (asInt(participant["cellId"], -1) == cellId) {
            return participant;
        }
    }
    return {};
}

bool pickCompletedForCell(const Json::Value& actions, const int cellId) {
    if (!actions.isArray()) {
        return false;
    }
    for (const Json::Value& phase : actions) {
        if (!phase.isArray()) {
            continue;
        }
        for (const Json::Value& action : phase) {
            if (asString(action["type"]) == "pick" &&
                asInt(action["actorCellId"], -1) == cellId &&
                action["completed"].asBool()) {
                return true;
            }
        }
    }
    return false;
}

std::string championNameFromParticipant(const Json::Value& participant, const bool allowIntent) {
    int championId = asInt(participant["championId"]);
    if (championId <= 0 && allowIntent) {
        championId = asInt(participant["championPickIntent"]);
    }
    return ChampionCatalog::nameForKey(championId);
}

std::string sameLaneEnemyChampion(const Json::Value& theirTeam, const std::string& myPosition) {
    if (!theirTeam.isArray()) {
        return "";
    }
    for (const Json::Value& enemy : theirTeam) {
        if (roleToOpgg(asString(enemy["assignedPosition"])) == roleToOpgg(myPosition)) {
            const std::string name = championNameFromParticipant(enemy, false);
            if (!name.empty()) {
                return name;
            }
        }
    }
    return "";
}

std::string firstEnemyChampion(const Json::Value& theirTeam) {
    if (!theirTeam.isArray()) {
        return "";
    }
    for (const Json::Value& enemy : theirTeam) {
        const std::string name = championNameFromParticipant(enemy, false);
        if (!name.empty()) {
            return name;
        }
    }
    return "";
}

std::string formatRunePageName(const std::string& champion, const std::string& position) {
    std::string name = "LOL Overlay: " + champion;
    if (!position.empty()) {
        name += " " + roleLabel(position);
    }
    if (name.size() > 45) {
        name.resize(45);
    }
    return name;
}

std::string runePageBody(const RuneRecommendation& recommendation, const std::string& pageName) {
    std::ostringstream body;
    body << R"({"name":")" << jsonEscape(pageName) << R"(","primaryStyleId":)"
         << recommendation.primaryStyleId << R"(,"subStyleId":)" << recommendation.subStyleId
         << R"(,"selectedPerkIds":[)";
    for (size_t i = 0; i < recommendation.selectedPerkIds.size(); ++i) {
        if (i > 0) {
            body << ',';
        }
        body << recommendation.selectedPerkIds[i];
    }
    body << R"(],"current":true})";
    return body.str();
}

std::string itemSetTitle(const std::string& champion, const std::string& position) {
    std::string title = "LOL Overlay Build: " + champion;
    if (!position.empty()) {
        title += " " + roleLabel(position);
    }
    if (title.size() > 50) {
        title.resize(50);
    }
    return title;
}

std::string itemSetKey(const int championId, const std::string& position, const ItemSetRecommendation& recommendation) {
    std::ostringstream key;
    key << championId << "|" << roleLabel(position) << "|core:";
    for (const int itemId : recommendation.coreItemIds) {
        key << itemId << ',';
    }
    key << "|boots:";
    for (const int itemId : recommendation.bootItemIds) {
        key << itemId << ',';
    }
    return key.str();
}

std::string jsonToString(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string jsonNumericId(const Json::Value& value) {
    if (value.isString()) {
        return value.asString();
    }
    if (value.isUInt64()) {
        return std::to_string(value.asLargestUInt());
    }
    if (value.isInt64()) {
        return std::to_string(value.asLargestInt());
    }
    if (value.isNumeric()) {
        return std::to_string(value.asInt());
    }
    return "";
}

Json::Value itemObject(const int itemId) {
    Json::Value item(Json::objectValue);
    item["id"] = std::to_string(itemId);
    item["count"] = 1;
    return item;
}

Json::Value itemSetBody(const ItemSetRecommendation& recommendation, const int championId,
                        const std::string& champion, const std::string& position) {
    Json::Value itemSet(Json::objectValue);
    itemSet["title"] = itemSetTitle(champion, position);
    itemSet["uid"] = "lol-overlay-build-" + std::to_string(championId) + "-" + roleLabel(position);
    itemSet["type"] = "custom";
    itemSet["map"] = "any";
    itemSet["mode"] = "any";
    itemSet["startedFrom"] = "blank";
    itemSet["sortrank"] = 0;

    Json::Value associatedChampions(Json::arrayValue);
    if (championId > 0) {
        associatedChampions.append(championId);
    }
    itemSet["associatedChampions"] = associatedChampions;

    Json::Value associatedMaps(Json::arrayValue);
    associatedMaps.append(11);
    itemSet["associatedMaps"] = associatedMaps;
    itemSet["preferredItemSlots"] = Json::Value(Json::arrayValue);

    Json::Value blocks(Json::arrayValue);
    if (!recommendation.coreItemIds.empty()) {
        Json::Value core(Json::objectValue);
        core["type"] = "Build Recommendation";
        Json::Value items(Json::arrayValue);
        for (const int itemId : recommendation.coreItemIds) {
            items.append(itemObject(itemId));
        }
        core["items"] = items;
        blocks.append(core);
    }
    if (!recommendation.bootItemIds.empty()) {
        Json::Value boots(Json::objectValue);
        boots["type"] = "Boots";
        Json::Value items(Json::arrayValue);
        for (const int itemId : recommendation.bootItemIds) {
            items.append(itemObject(itemId));
        }
        boots["items"] = items;
        blocks.append(boots);
    }
    itemSet["blocks"] = blocks;
    return itemSet;
}

bool importItemSet(const LcuConnection& connection, const ItemSetRecommendation& recommendation,
                   const int championId, const std::string& champion, const std::string& position,
                   std::string& status) {
    if (!recommendation.ok) {
        status = "No OP.GG item build available yet.";
        return false;
    }

    const HttpResponse summonerResponse = LcuClient::get(connection, "/lol-summoner/v1/current-summoner");
    if (!summonerResponse.ok) {
        status = "Item set import failed: current summoner unavailable.";
        return false;
    }

    Json::Value summoner;
    std::string parseError;
    if (!parseApiJson(summonerResponse.body, summoner, parseError)) {
        status = "Item set import failed: summoner data invalid.";
        return false;
    }

    const std::string summonerId = jsonNumericId(summoner["summonerId"]);
    if (summonerId.empty()) {
        status = "Item set import failed: missing summoner id.";
        return false;
    }

    const Json::Value newItemSet = itemSetBody(recommendation, championId, champion, position);
    const std::string title = asString(newItemSet["title"]);
    const std::string uid = asString(newItemSet["uid"]);
    const std::string path = "/lol-item-sets/v1/item-sets/" + summonerId + "/sets";

    const HttpResponse getResponse = LcuClient::get(connection, path);
    if (getResponse.ok) {
        Json::Value collection;
        if (parseApiJson(getResponse.body, collection, parseError) && collection.isObject()) {
            Json::Value nextSets(Json::arrayValue);
            const Json::Value& existingSets = collection["itemSets"];
            if (existingSets.isArray()) {
                for (const Json::Value& set : existingSets) {
                    if (asString(set["title"]) != title && asString(set["uid"]) != uid) {
                        nextSets.append(set);
                    }
                }
            }
            nextSets.append(newItemSet);
            collection["itemSets"] = nextSets;
            if (!collection["accountId"].isNumeric() && !collection["accountId"].isString()) {
                try {
                    collection["accountId"] = static_cast<Json::UInt64>(std::stoull(summonerId));
                } catch (...) {
                    collection["accountId"] = summonerId;
                }
            }
            collection["timestamp"] = static_cast<Json::UInt64>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            const HttpResponse putResponse = LcuClient::putJson(connection, path, jsonToString(collection));
            if (putResponse.ok) {
                status = "Item set imported/updated for in-game shop.";
                return true;
            }
            status = "Item set import failed: " + (putResponse.error.empty() ? "LCU rejected update" : putResponse.error);
            return false;
        }
    }

    const HttpResponse postResponse = LcuClient::postJson(connection, path, jsonToString(newItemSet));
    if (!postResponse.ok) {
        status = "Item set import failed: " + (postResponse.error.empty() ? "LCU rejected item set" : postResponse.error);
        return false;
    }

    status = "Item set imported for in-game shop.";
    return true;
}

bool importRunePage(const LcuConnection& connection, const RuneRecommendation& recommendation,
                    const std::string& champion, const std::string& position, std::string& status) {
    if (!recommendation.ok) {
        status = "No OP.GG rune page available yet.";
        return false;
    }

    const std::string pageName = formatRunePageName(champion, position);
    const std::string body = runePageBody(recommendation, pageName);
    const HttpResponse pagesResponse = LcuClient::get(connection, "/lol-perks/v1/pages");
    if (pagesResponse.ok) {
        Json::Value pages;
        std::string parseError;
        if (parseApiJson(pagesResponse.body, pages, parseError) && pages.isArray()) {
            for (const Json::Value& page : pages) {
                const std::string name = asString(page["name"]);
                if (name.rfind("LOL Overlay:", 0) == 0 && page["id"].isNumeric()) {
                    const HttpResponse updateResponse = LcuClient::putJson(
                        connection, "/lol-perks/v1/pages/" + std::to_string(page["id"].asInt()), body);
                    if (updateResponse.ok) {
                        status = "Imported runes into existing LOL Overlay page.";
                        return true;
                    }
                }
            }
        }
    }

    const HttpResponse createResponse = LcuClient::postJson(connection, "/lol-perks/v1/pages", body);
    if (!createResponse.ok) {
        status = "Rune import failed: " + (createResponse.error.empty() ? "LCU rejected page" : createResponse.error);
        return false;
    }

    status = "Imported runes into a new LOL Overlay page.";
    return true;
}

EnemyChampionCounter counterForEnemy(const std::string& enemyName, const std::string& position,
                                     const std::vector<CounterRecord>& counters) {
    EnemyChampionCounter result;
    result.championName = enemyName;
    result.position = position;
    result.relation = "NA";
    const std::string enemyKey = ChampionCatalog::opggNameForChampion(enemyName);
    for (const CounterRecord& counter : counters) {
        if (ChampionCatalog::opggNameForChampion(counter.championName) == enemyKey) {
            result.relation = counter.relation;
            result.myWinRate = counter.myWinRate;
            result.games = counter.games;
            return result;
        }
    }
    return result;
}
}

bool pollChampSelectState(ChampSelectState& state, const bool autoImportRunes) {
    state = {};
    const LcuConnection connection = LcuClient::discover();
    if (!connection.ok) {
        state.status = connection.error;
        return false;
    }

    const HttpResponse sessionResponse = LcuClient::get(connection, "/lol-champ-select/v1/session");
    if (!sessionResponse.ok) {
        state.status = "Not in champion select.";
        return false;
    }

    Json::Value session;
    std::string parseError;
    if (!parseApiJson(sessionResponse.body, session, parseError) || !session.isObject()) {
        state.status = "Champion select returned invalid data.";
        return false;
    }

    state.visible = true;
    const int localCellId = asInt(session["localPlayerCellId"], -1);
    const Json::Value localPlayer = findParticipantByCell(session["myTeam"], localCellId);
    state.myChampionId = asInt(localPlayer["championId"]);
    if (state.myChampionId <= 0) {
        state.myChampionId = asInt(localPlayer["championPickIntent"]);
    }
    state.myChampionName = ChampionCatalog::nameForKey(state.myChampionId);
    state.myPosition = roleLabel(asString(localPlayer["assignedPosition"]));
    state.myChampionLocked = pickCompletedForCell(session["actions"], localCellId) && asInt(localPlayer["championId"]) > 0;

    const AnalysisCache analysis = state.myChampionName.empty()
        ? AnalysisCache{}
        : fetchChampionAnalysis(state.myChampionName, state.myPosition);

    struct VisibleEnemy {
        std::string name;
        std::string position;
    };
    std::vector<VisibleEnemy> visibleEnemies;
    if (session["theirTeam"].isArray()) {
        for (const Json::Value& enemy : session["theirTeam"]) {
            const std::string enemyName = championNameFromParticipant(enemy, false);
            if (!enemyName.empty()) {
                visibleEnemies.push_back(VisibleEnemy{enemyName, roleLabel(asString(enemy["assignedPosition"]))});
            }
        }
    }

    for (const VisibleEnemy& enemy : visibleEnemies) {
        EnemyChampionCounter counter = counterForEnemy(enemy.name, enemy.position, analysis.counters);
        if (!state.myChampionName.empty() && counter.myWinRate < 0.0) {
            const std::string matchupPosition = enemy.position.empty() || enemy.position == "ANY"
                ? state.myPosition
                : enemy.position;
            const MatchupGuideRecommendation guide =
                fetchMatchupGuide(state.myChampionName, enemy.name, matchupPosition);
            if (guide.counterOk) {
                counter.position = guide.counter.position.empty() ? enemy.position : guide.counter.position;
                counter.relation = guide.counter.relation;
                counter.myWinRate = guide.counter.myWinRate;
                counter.games = guide.counter.games;
            } else if (guide.counter.relation != "NA") {
                counter.position = guide.counter.position.empty() ? enemy.position : guide.counter.position;
                counter.relation = guide.counter.relation;
            }
        }
        state.enemyCounters.push_back(counter);
    }

    if (!state.myChampionName.empty()) {
        const std::string laneEnemy = sameLaneEnemyChampion(session["theirTeam"], state.myPosition);
        const std::string guideEnemy = laneEnemy.empty() ? firstEnemyChampion(session["theirTeam"]) : laneEnemy;
        const MatchupGuideRecommendation guide = fetchMatchupGuide(state.myChampionName, guideEnemy, state.myPosition);

        RuneRecommendation recommendation = guide.runes;
        if (!recommendation.ok) {
            recommendation = analysis.genericRunes;
        }

        ItemSetRecommendation itemRecommendation = guide.items;
        if (!itemRecommendation.ok) {
            itemRecommendation = analysis.genericItems;
        }
        state.runeLines = recommendation.lines;
        state.itemSetLines = itemRecommendation.lines;

        const std::string runeKey = std::to_string(state.myChampionId) + "|" + state.myPosition + "|" +
                                    std::to_string(recommendation.primaryStyleId) + "|" +
                                    std::to_string(recommendation.subStyleId);
        if (autoImportRunes && state.myChampionLocked && recommendation.ok && importedRuneKey != runeKey) {
            if (importRunePage(connection, recommendation, state.myChampionName, state.myPosition, state.runeStatus)) {
                importedRuneKey = runeKey;
            }
        } else if (state.myChampionLocked && importedRuneKey == runeKey) {
            state.runeStatus = "Runes already imported for this lock.";
        } else if (state.myChampionLocked) {
            state.runeStatus = recommendation.ok ? "Ready to import runes." : "No rune recommendation available.";
        } else {
            state.runeStatus = recommendation.ok ? "Lock your champion to auto-import runes." : "Waiting for OP.GG runes.";
        }

        const std::string currentItemSetKey = itemSetKey(state.myChampionId, state.myPosition, itemRecommendation);
        if (autoImportRunes && state.myChampionLocked && itemRecommendation.ok && importedItemSetKey != currentItemSetKey) {
            if (importItemSet(connection, itemRecommendation, state.myChampionId,
                              state.myChampionName, state.myPosition, state.itemSetStatus)) {
                importedItemSetKey = currentItemSetKey;
            }
        } else if (state.myChampionLocked && importedItemSetKey == currentItemSetKey) {
            state.itemSetStatus = "Item set already imported for this lock.";
        } else if (state.myChampionLocked) {
            state.itemSetStatus = itemRecommendation.ok ? "Ready to import item set." : "No item recommendation available.";
        } else {
            state.itemSetStatus = itemRecommendation.ok ? "Lock your champion to auto-import item set."
                                                        : "Waiting for OP.GG item build.";
        }
    } else {
        state.runeStatus = "Hover or lock a champion to load runes.";
        state.itemSetStatus = "Hover or lock a champion to load item build.";
    }

    if (state.enemyCounters.empty()) {
        state.status = "Waiting for visible enemy picks.";
    } else if (state.myChampionName.empty()) {
        state.status = "Enemy picks visible. Select your champion for counter WR.";
    } else {
        state.status = "Champion select data active.";
    }

    return true;
}
