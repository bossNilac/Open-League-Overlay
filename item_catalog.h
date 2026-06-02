#ifndef LOL_OVERLAY_ITEM_CATALOG_H
#define LOL_OVERLAY_ITEM_CATALOG_H

#include <json/json.h>

namespace ItemCatalog {
int itemValue(const Json::Value& item);
int inventoryValue(const Json::Value& items);
}

#endif // LOL_OVERLAY_ITEM_CATALOG_H
