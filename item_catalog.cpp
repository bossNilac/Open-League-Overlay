#include "item_catalog.h"

#include "api/api_caller.h"
#include "api/json_parser.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
std::once_flag catalogLoadOnce;
std::unordered_map<int, int> itemGoldTotals;

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

void loadItemGoldTotals() {
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

    const std::string itemUrl = "https://ddragon.leagueoflegends.com/cdn/" + version + "/data/en_US/item.json";
    const HttpResponse itemResponse = ApiCaller::getInstance()->get(itemUrl);
    if (!itemResponse.ok) {
        return;
    }

    Json::Value root;
    if (!parseApiJson(itemResponse.body, root, parseError) || !root["data"].isObject()) {
        return;
    }

    const Json::Value& data = root["data"];
    for (const std::string& key : data.getMemberNames()) {
        const int itemId = std::atoi(key.c_str());
        if (itemId <= 0) {
            continue;
        }

        const int total = asInt(data[key]["gold"]["total"], -1);
        if (total >= 0) {
            itemGoldTotals[itemId] = total;
        }
    }
}

int itemIdFor(const Json::Value& item) {
    if (item["itemID"].isNumeric()) {
        return item["itemID"].asInt();
    }
    if (item["itemId"].isNumeric()) {
        return item["itemId"].asInt();
    }
    if (item["id"].isNumeric()) {
        return item["id"].asInt();
    }
    return 0;
}
}

namespace ItemCatalog {
int itemValue(const Json::Value& item) {
    const int count = std::max(1, asInt(item["count"], 1));
    const int liveFallback = asInt(item["price"]) * count;
    const int itemId = itemIdFor(item);
    if (itemId <= 0) {
        return liveFallback;
    }

    std::call_once(catalogLoadOnce, loadItemGoldTotals);
    const auto found = itemGoldTotals.find(itemId);
    if (found == itemGoldTotals.end()) {
        return liveFallback;
    }

    return found->second * count;
}

int inventoryValue(const Json::Value& items) {
    if (!items.isArray()) {
        return 0;
    }

    int total = 0;
    for (const auto& item : items) {
        total += itemValue(item);
    }
    return total;
}
}
