#ifndef LOL_OVERLAY_CHAMPION_CATALOG_H
#define LOL_OVERLAY_CHAMPION_CATALOG_H

#include <string>

namespace ChampionCatalog {
std::string nameForKey(int championKey);
std::string opggNameForChampion(const std::string& champion);
}

#endif // LOL_OVERLAY_CHAMPION_CATALOG_H
