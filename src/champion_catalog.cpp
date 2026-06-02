#include "champion_catalog.h"

#include "api/api_caller.h"
#include "api/json_parser.h"

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
std::once_flag championLoadOnce;
std::unordered_map<int, std::string> championNames;

std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

void loadChampionNames() {
    const HttpResponse versionsResponse = ApiCaller::getInstance()->get(
        "https://ddragon.leagueoflegends.com/api/versions.json");
    if (!versionsResponse.ok) {
        return;
    }

    Json::Value versions;
    std::string parseError;
    if (!parseApiJson(versionsResponse.body, versions, parseError) || !versions.isArray() || versions.empty()) {
        return;
    }

    const std::string version = asString(versions[0]);
    if (version.empty()) {
        return;
    }

    const std::string championUrl = "https://ddragon.leagueoflegends.com/cdn/" + version + "/data/en_US/champion.json";
    const HttpResponse championResponse = ApiCaller::getInstance()->get(championUrl);
    if (!championResponse.ok) {
        return;
    }

    Json::Value root;
    if (!parseApiJson(championResponse.body, root, parseError) || !root["data"].isObject()) {
        return;
    }

    const Json::Value& data = root["data"];
    for (const std::string& member : data.getMemberNames()) {
        const Json::Value& champion = data[member];
        const int key = std::atoi(asString(champion["key"]).c_str());
        const std::string name = asString(champion["name"]);
        if (key > 0 && !name.empty()) {
            championNames[key] = name;
        }
    }
}
}

namespace ChampionCatalog {
std::string nameForKey(const int championKey) {
    if (championKey <= 0) {
        return "";
    }

    std::call_once(championLoadOnce, loadChampionNames);
    const auto found = championNames.find(championKey);
    if (found == championNames.end()) {
        return "";
    }
    return found->second;
}

std::string opggNameForChampion(const std::string& champion) {
    std::string normalized;
    for (const unsigned char ch : champion) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}
}
