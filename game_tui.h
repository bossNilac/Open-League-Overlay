#ifndef LOL_OVERLAY_GAME_TUI_H
#define LOL_OVERLAY_GAME_TUI_H

#include <json/json.h>

#include <string>

struct ChampSelectState;

class GameTui {
public:
    GameTui(int refreshMs, bool once, int consoleFontHeight = 0, bool autoScaleConsoleFont = true);

    int run();

private:
    int refreshMs;
    bool once;
    int consoleFontHeight;
    bool autoScaleConsoleFont;
    bool terminalInitialized = false;
    int lastTerminalWidth = 0;
    int lastTerminalHeight = 0;
    bool guiRequested = false;
    bool showDetailedStats = false;
    int itemLineMode = 0; // 0 auto, 1 show, 2 hide
    std::string lastFrame;

    bool pollGameData(Json::Value& root, std::string& error) const;
    void renderGame(const Json::Value& root);
    void renderChampSelect(const ChampSelectState& state);
    void renderWaiting(const std::string& error);
    void presentFrame(const std::string& frame);
    bool waitForNextFrame();
};

#endif // LOL_OVERLAY_GAME_TUI_H
