#ifndef LOL_OVERLAY_APP_SETTINGS_H
#define LOL_OVERLAY_APP_SETTINGS_H

struct AppSettings {
    bool startWithWindows = false;
    bool firstRunStartupPromptShown = false;
};

namespace AppSettingsStore {
AppSettings load();
void save(const AppSettings& settings);
bool startupRegistryEnabled();
bool enableStartWithWindows();
bool disableStartWithWindows();
void syncStartupRegistry(const AppSettings& settings);
}

#endif // LOL_OVERLAY_APP_SETTINGS_H
