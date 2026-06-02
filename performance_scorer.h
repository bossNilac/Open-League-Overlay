#ifndef LOL_OVERLAY_PERFORMANCE_SCORER_H
#define LOL_OVERLAY_PERFORMANCE_SCORER_H

#include <json/json.h>

#include <string>

namespace PerformanceScorer {
Json::Value score(const Json::Value& finalSnapshot);
std::string gradeForScore(int score);
}

#endif // LOL_OVERLAY_PERFORMANCE_SCORER_H
