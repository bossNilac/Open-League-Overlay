#ifndef LOL_OVERLAY_PERFORMANCE_TRACKER_H
#define LOL_OVERLAY_PERFORMANCE_TRACKER_H

#include <json/json.h>

namespace PerformanceTracker {
void observeLiveGame(const Json::Value& root);
void observeGameUnavailable();
}

#endif // LOL_OVERLAY_PERFORMANCE_TRACKER_H
