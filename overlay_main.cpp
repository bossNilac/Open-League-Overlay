#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <windowsx.h>

#include "app_paths.h"
#include "app_settings.h"
#include "api/api_caller.h"
#include "api/json_parser.h"
#include "champ_select.h"
#include "match_history.h"
#include "overlay_data.h"
#include "performance_tracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr UINT_PTR PollTimerId = 1;
constexpr int PollIntervalMs = 1000;
constexpr int OverlayCsRefreshSeconds = 5;
constexpr size_t HistoryPageSize = 10;
constexpr int HotkeyNudgeLeft = 2;
constexpr int HotkeyNudgeRight = 3;
constexpr int HotkeyNudgeUp = 4;
constexpr int HotkeyNudgeDown = 5;
constexpr int HotkeyDisplayMode = 6;
constexpr UINT ShowScoreboardMessage = WM_APP + 1;
constexpr UINT StartupPromptMessage = WM_APP + 2;
constexpr COLORREF TransparentColor = RGB(1, 1, 1);
constexpr wchar_t OverlayWindowClassName[] = L"LOLOverlayLightweightGui";

constexpr COLORREF PanelColor = RGB(13, 18, 24);
constexpr COLORREF PanelSoftColor = RGB(18, 26, 34);
constexpr COLORREF BorderColor = RGB(48, 65, 78);
constexpr COLORREF TextColor = RGB(235, 240, 244);
constexpr COLORREF MutedColor = RGB(160, 169, 178);
constexpr COLORREF AllyColor = RGB(94, 224, 214);
constexpr COLORREF EnemyColor = RGB(255, 124, 146);
constexpr COLORREF GoldColor = RGB(255, 211, 92);
constexpr COLORREF VisionColor = RGB(184, 138, 255);
constexpr COLORREF GoodColor = RGB(126, 232, 154);
constexpr COLORREF BackgroundEraseColor = TransparentColor;
constexpr BYTE OverlayAlpha = 205;

enum class DisplayMode {
    FullTeams = 0,
    LaneOnly = 1,
    SelfOnly = 2,
    Scoreboard = 3
};

enum class GuiPage {
    Home = 0,
    History = 1,
    Detail = 2
};

struct OverlaySize {
    int width = 310;
    int height = 360;
};

struct OverlayConfig {
    int x = 36;
    int y = 150;
    int mode = 0;
};

struct OverlayCsSnapshot {
    bool valid = false;
    bool enemyKnown = false;
    int myCreepScore = 0;
    int enemyCreepScore = 0;
    double myCsPerMinute = 0.0;
    double enemyCsPerMinute = 0.0;
    std::string myChampion;
    std::string enemyChampion;
    std::string myPosition;
    std::chrono::steady_clock::time_point lastUpdate{};
};

struct AppState {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    bool visible = false;
    DisplayMode displayMode = DisplayMode::FullTeams;
    LaneOverlayStats stats;
    ChampSelectState champSelect;
    OverlayCsSnapshot csSnapshot;
    OverlayConfig config;
    std::filesystem::path configPath;
    bool startScoreboard = false;
    std::string metaKey;
    std::vector<std::wstring> metaLines;
    std::chrono::steady_clock::time_point lastMetaFetch{};
    GuiPage guiPage = GuiPage::Home;
    std::vector<MatchHistorySummary> history;
    std::vector<MatchHistorySummary> homeHistory;
    std::string historyFilter;
    bool historyLoaded = false;
    bool homeHistoryLoaded = false;
    bool historyHasMore = false;
    bool historyLoadingMore = false;
    size_t historyLoadedCount = 0;
    size_t savedMatchCount = 0;
    AppSettings settings;
    Json::Value selectedMatch;
    Json::Value selectedReport;
    int selectedHistoryIndex = -1;
    RECT historyButtonRect{};
    RECT lastReportButtonRect{};
    RECT startupToggleRect{};
    RECT backButtonRect{};
    RECT searchRect{};
    std::vector<RECT> homeRecentRects;
    std::vector<RECT> historyCardRects;
    int historyScroll = 0;
    int historyContentHeight = 0;
    int detailScroll = 0;
    int detailContentHeight = 0;
    bool movingWindow = false;
    std::chrono::steady_clock::time_point lastChampSelectPoll{};
};

AppState g_state;

void refreshMetaForSelf(const LaneOverlayStats& stats);

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::wstring formatDouble(const double value, const int precision = 1) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string asString(const Json::Value& value, const std::string& fallback = "") {
    return value.isString() ? value.asString() : fallback;
}

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

double asDouble(const Json::Value& value, const double fallback = 0.0) {
    return value.isNumeric() ? value.asDouble() : fallback;
}

std::wstring kdaText(const int kills, const int deaths, const int assists) {
    std::wostringstream out;
    out << kills << L'/' << deaths << L'/' << assists;
    return out.str();
}

std::wstring formatInt(const int value) {
    const bool negative = value < 0;
    std::string digits = std::to_string(std::abs(value));
    std::string formatted;

    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            formatted.push_back(',');
        }
        formatted.push_back(digits[i]);
    }

    if (negative) {
        formatted.insert(formatted.begin(), '-');
    }

    return std::wstring(formatted.begin(), formatted.end());
}

std::wstring formatSignedGold(const int value) {
    if (value > 0) {
        return L"+" + formatInt(value) + L"g";
    }
    if (value < 0) {
        return L"-" + formatInt(std::abs(value)) + L"g";
    }
    return L"even";
}

std::wstring formatSignedNumber(const int value) {
    if (value > 0) {
        return L"+" + formatInt(value);
    }
    if (value < 0) {
        return L"-" + formatInt(std::abs(value));
    }
    return L"even";
}

std::wstring formatGold(const int value) {
    return formatInt(value) + L"g";
}

std::string normalizeOpggChampion(std::string champion) {
    std::string normalized;
    for (const unsigned char ch : champion) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}

std::string opggPosition(const std::string& position) {
    if (position == "TOP") return "top";
    if (position == "JG" || position == "JUNGLE") return "jungle";
    if (position == "MID" || position == "MIDDLE") return "mid";
    if (position == "BOT" || position == "BOTTOM" || position == "ADC") return "adc";
    if (position == "SUP" || position == "UTILITY" || position == "SUPPORT") return "support";
    return "top";
}

bool sameOpggPosition(const std::string& left, const std::string& right) {
    return opggPosition(left) == opggPosition(right);
}

bool hasChampionName(const std::string& champion) {
    return !normalizeOpggChampion(champion).empty();
}

struct MetaTarget {
    bool ok = false;
    std::string myChampion;
    std::string enemyChampion;
    std::string positionLabel;
    std::string opggPosition;
};

MetaTarget metaTargetFromRow(const RoleInventoryRow& row, const bool myBlue) {
    MetaTarget target;
    target.myChampion = myBlue ? row.blueChampion : row.redChampion;
    target.enemyChampion = myBlue ? row.redChampion : row.blueChampion;
    target.positionLabel = row.role;
    target.opggPosition = opggPosition(row.role);
    target.ok = hasChampionName(target.myChampion) && hasChampionName(target.enemyChampion);
    return target;
}

MetaTarget metaTargetForSelf(const LaneOverlayStats& stats) {
    if (stats.enemyKnown && hasChampionName(stats.myChampion) && hasChampionName(stats.enemyChampion)) {
        return MetaTarget{true, stats.myChampion, stats.enemyChampion, stats.myPosition, opggPosition(stats.myPosition)};
    }

    const bool knownBlueSide = stats.myTeam == "ORDER";
    const bool knownRedSide = stats.myTeam == "CHAOS";
    if (knownBlueSide || knownRedSide) {
        for (const RoleInventoryRow& row : stats.roleInventoryRows) {
            if (sameOpggPosition(row.role, stats.myPosition)) {
                MetaTarget target = metaTargetFromRow(row, knownBlueSide);
                if (!hasChampionName(target.myChampion) && hasChampionName(stats.myChampion)) {
                    target.myChampion = stats.myChampion;
                }
                target.ok = hasChampionName(target.myChampion) && hasChampionName(target.enemyChampion);
                if (target.ok) {
                    return target;
                }
            }
        }
    }

    const std::string myNormalized = normalizeOpggChampion(stats.myChampion);
    if (!myNormalized.empty()) {
        for (const RoleInventoryRow& row : stats.roleInventoryRows) {
            if (normalizeOpggChampion(row.blueChampion) == myNormalized) {
                MetaTarget target = metaTargetFromRow(row, true);
                if (target.ok) {
                    return target;
                }
            }
            if (normalizeOpggChampion(row.redChampion) == myNormalized) {
                MetaTarget target = metaTargetFromRow(row, false);
                if (target.ok) {
                    return target;
                }
            }
        }
    }

    return {};
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

std::wstring joinJsonStringArray(const Json::Value& value, const size_t maxItems = 6) {
    if (!value.isArray()) {
        return L"-";
    }

    std::wstring result;
    const Json::ArrayIndex count = std::min<Json::ArrayIndex>(value.size(), static_cast<Json::ArrayIndex>(maxItems));
    for (Json::ArrayIndex i = 0; i < count; ++i) {
        if (!result.empty()) {
            result += L", ";
        }
        result += utf8ToWide(value[i].isString() ? value[i].asString() : value[i].asString());
    }
    return result.empty() ? L"-" : result;
}

std::wstring championLabel(const std::string& champion) {
    std::wstring label = utf8ToWide(champion.empty() ? "-" : champion);
    if (label.size() > 7) {
        label = label.substr(0, 6) + L".";
    }
    return label;
}

std::wstring compactLabel(const std::string& text, const size_t maxChars) {
    std::wstring label = utf8ToWide(text.empty() ? "-" : text);
    if (label.size() > maxChars && maxChars > 1) {
        label = label.substr(0, maxChars - 1) + L".";
    }
    return label;
}

RECT monitorWorkAreaForOverlay() {
    POINT point{g_state.config.x, g_state.config.y};
    if (g_state.hwnd) {
        RECT rect{};
        if (GetWindowRect(g_state.hwnd, &rect)) {
            point = POINT{rect.left, rect.top};
        }
    }

    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return info.rcWork;
    }

    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

OverlaySize overlaySizeForMode(const DisplayMode mode) {
    switch (mode) {
        case DisplayMode::SelfOnly:
            return {204, 112};
        case DisplayMode::LaneOnly:
            return {224, 224};
        case DisplayMode::Scoreboard: {
            const RECT work = monitorWorkAreaForOverlay();
            const int workWidth = work.right - work.left;
            const int workHeight = work.bottom - work.top;
            return {
                std::max(720, std::min(1040, workWidth - 80)),
                std::max(380, std::min(620, workHeight - 100))
            };
        }
        case DisplayMode::FullTeams:
        default:
            return {370, 196};
    }
}

OverlaySize windowSizeForMode(const DisplayMode mode) {
    const OverlaySize clientSize = overlaySizeForMode(mode);
    if (mode != DisplayMode::Scoreboard) {
        return clientSize;
    }

    const DWORD exStyle = WS_EX_APPWINDOW;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect{0, 0, clientSize.width, clientSize.height};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    return {rect.right - rect.left, rect.bottom - rect.top};
}

DisplayMode activeDisplayMode() {
    if (g_state.champSelect.visible && !g_state.startScoreboard) {
        return DisplayMode::FullTeams;
    }
    return g_state.displayMode;
}

DisplayMode nextDisplayMode(const DisplayMode mode) {
    switch (mode) {
        case DisplayMode::FullTeams:
            return DisplayMode::LaneOnly;
        case DisplayMode::LaneOnly:
            return DisplayMode::SelfOnly;
        case DisplayMode::SelfOnly:
        case DisplayMode::Scoreboard:
        default:
            return DisplayMode::FullTeams;
    }
}

std::wstring modeButtonText(const DisplayMode mode) {
    switch (mode) {
        case DisplayMode::FullTeams:
            return L"FULL";
        case DisplayMode::LaneOnly:
            return L"LANE";
        case DisplayMode::Scoreboard:
            return L"TUI";
        case DisplayMode::SelfOnly:
        default:
            return L"YOU";
    }
}

RECT modeButtonRect(const DisplayMode mode) {
    const OverlaySize size = overlaySizeForMode(mode);
    return RECT{size.width - 48, 5, size.width - 6, 24};
}

bool scoreboardWindow() {
    return g_state.startScoreboard;
}

bool processNameEquals(const wchar_t* lhs, const wchar_t* rhs) {
    return _wcsicmp(lhs, rhs) == 0;
}

std::wstring processNameForPid(const DWORD processId) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::wstring name;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == processId) {
                name = entry.szExeFile;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return name;
}

bool leagueSurfaceProcessName(const std::wstring& name) {
    return processNameEquals(name.c_str(), L"League of Legends.exe") ||
           processNameEquals(name.c_str(), L"LeagueClient.exe") ||
           processNameEquals(name.c_str(), L"LeagueClientUx.exe") ||
           processNameEquals(name.c_str(), L"LeagueClientUxRender.exe");
}

bool leagueSurfaceFocused() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!processId) {
        return false;
    }

    return leagueSurfaceProcessName(processNameForPid(processId));
}

bool pointInRect(const POINT point, const RECT rect) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

std::wstring teamSideName(const bool isBlue) {
    return isBlue ? L"BLUE" : L"RED";
}

std::filesystem::path moduleDirectory() {
    return AppPaths::moduleDirectory();
}

std::filesystem::path performanceDirectory() {
    return AppPaths::matchHistoryDirectory();
}

void refreshHistory(const bool force = false) {
    if (!scoreboardWindow()) {
        return;
    }
    if (!force && g_state.historyLoaded) {
        return;
    }
    bool hasMore = false;
    g_state.history = MatchHistory::loadSummaryPage(performanceDirectory(), g_state.historyFilter, 0, HistoryPageSize, hasMore);
    g_state.historyLoadedCount = g_state.history.size();
    g_state.historyHasMore = hasMore;
    g_state.historyLoaded = true;
    g_state.historyLoadingMore = false;
}

void resetHistoryPaging() {
    g_state.history.clear();
    g_state.historyLoaded = false;
    g_state.historyHasMore = false;
    g_state.historyLoadingMore = false;
    g_state.historyLoadedCount = 0;
    g_state.historyCardRects.clear();
}

void loadMoreHistoryIfNeeded(const bool force = false) {
    if (!scoreboardWindow() || g_state.historyLoadingMore) {
        return;
    }
    if (!g_state.historyLoaded) {
        refreshHistory(true);
        return;
    }
    if (!g_state.historyHasMore && !force) {
        return;
    }

    g_state.historyLoadingMore = true;
    bool hasMore = false;
    std::vector<MatchHistorySummary> next =
        MatchHistory::loadSummaryPage(performanceDirectory(), g_state.historyFilter,
                                      g_state.historyLoadedCount, HistoryPageSize, hasMore);
    g_state.history.insert(g_state.history.end(), next.begin(), next.end());
    g_state.historyLoadedCount += next.size();
    g_state.historyHasMore = hasMore;
    g_state.historyLoadingMore = false;
}

void refreshHomeHistory(const bool force = false) {
    if (!scoreboardWindow()) {
        return;
    }
    if (!force && g_state.homeHistoryLoaded) {
        return;
    }
    bool hasMore = false;
    g_state.savedMatchCount = MatchHistory::countMatchFiles(performanceDirectory());
    g_state.homeHistory = MatchHistory::loadSummaryPage(performanceDirectory(), "", 0, 100, hasMore);
    g_state.homeHistoryLoaded = true;
}

void saveSettingsAndSyncStartup() {
    AppSettingsStore::save(g_state.settings);
    AppSettingsStore::syncStartupRegistry(g_state.settings);
}

void promptStartWithWindowsIfNeeded() {
    if (!scoreboardWindow() || g_state.settings.firstRunStartupPromptShown) {
        return;
    }

    const int answer = MessageBoxW(g_state.hwnd,
                                   L"Start this app automatically when Windows starts?\n\n"
                                   L"Choose Yes to enable startup. Choose No for Not now.",
                                   L"Open League Overlay",
                                   MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    g_state.settings.firstRunStartupPromptShown = true;
    g_state.settings.startWithWindows = answer == IDYES;
    saveSettingsAndSyncStartup();
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void toggleStartWithWindows() {
    g_state.settings.startWithWindows = !g_state.settings.startWithWindows;
    g_state.settings.firstRunStartupPromptShown = true;
    saveSettingsAndSyncStartup();
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void launchOverlayCompanionIfNeeded() {
    if (!g_state.startScoreboard) {
        return;
    }

    const std::filesystem::path overlayPath = moduleDirectory() / "LOL_overlay_gui.exe";
    if (!std::filesystem::exists(overlayPath)) {
        return;
    }

    std::wstring commandLine = L"\"" + overlayPath.wstring() + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(overlayPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_PROCESS_GROUP, nullptr, overlayPath.parent_path().c_str(),
                       &startupInfo, &processInfo)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
}

void closeOverlayCompanionIfNeeded() {
    if (!g_state.startScoreboard) {
        return;
    }

    HWND overlay = FindWindowW(OverlayWindowClassName, nullptr);
    if (overlay && overlay != g_state.hwnd) {
        PostMessageW(overlay, WM_CLOSE, 0, 0);
    }
}

OverlayConfig loadConfig(const std::filesystem::path& configPath) {
    OverlayConfig config;
    std::ifstream input(configPath);
    std::string line;
    while (std::getline(input, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, equals);
        const int value = std::atoi(line.substr(equals + 1).c_str());
        if (key == "x") {
            config.x = value;
        } else if (key == "y") {
            config.y = value;
        } else if (key == "mode") {
            config.mode = std::clamp(value, 0, 3);
        }
    }
    return config;
}

void saveConfig() {
    if (g_state.configPath.empty()) {
        return;
    }

    RECT rect{};
    if (g_state.hwnd && GetWindowRect(g_state.hwnd, &rect)) {
        g_state.config.x = rect.left;
        g_state.config.y = rect.top;
    }

    std::ofstream output(g_state.configPath, std::ios::trunc);
    output << "x=" << g_state.config.x << '\n';
    output << "y=" << g_state.config.y << '\n';
    output << "mode=" << static_cast<int>(g_state.displayMode) << '\n';
}

bool fetchOverlayStats(LaneOverlayStats& stats) {
    const HttpResponse response = ApiCaller::getInstance()->getLiveEndpoint("allgamedata");
    if (!response.ok) {
        return false;
    }

    Json::Value root;
    std::string parseError;
    if (!parseApiJson(response.body, root, parseError)) {
        return false;
    }

    PerformanceTracker::observeLiveGame(root);
    return buildLaneOverlayStats(root, stats);
}

void hideOverlay() {
    if (scoreboardWindow()) {
        if (!g_state.visible) {
            ShowWindow(g_state.hwnd, SW_SHOWNORMAL);
            g_state.visible = true;
        }
        g_state.stats = LaneOverlayStats{};
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }

    if (g_state.visible) {
        ShowWindow(g_state.hwnd, SW_HIDE);
        g_state.visible = false;
    }
    g_state.stats = LaneOverlayStats{};
}

void showOverlay() {
    if (!g_state.visible) {
        ShowWindow(g_state.hwnd, scoreboardWindow() ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
        g_state.visible = true;
    }
    const OverlaySize size = overlaySizeForMode(activeDisplayMode());
    const OverlaySize windowSize = windowSizeForMode(activeDisplayMode());
    SetWindowPos(g_state.hwnd, scoreboardWindow() ? HWND_NOTOPMOST : HWND_TOPMOST,
                 g_state.config.x, g_state.config.y, windowSize.width, windowSize.height,
                 scoreboardWindow() ? 0 : SWP_NOACTIVATE);
}

void resizeOverlay() {
    const OverlaySize size = windowSizeForMode(activeDisplayMode());
    SetWindowPos(g_state.hwnd, scoreboardWindow() ? HWND_NOTOPMOST : HWND_TOPMOST,
                 g_state.config.x, g_state.config.y, size.width, size.height,
                 scoreboardWindow() ? 0 : SWP_NOACTIVATE);
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

bool updateOverlayCsSnapshot(const LaneOverlayStats& stats) {
    if (!stats.visible) {
        const bool hadSnapshot = g_state.csSnapshot.valid;
        g_state.csSnapshot = {};
        return hadSnapshot;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool identityChanged =
        !g_state.csSnapshot.valid ||
        g_state.csSnapshot.myChampion != stats.myChampion ||
        g_state.csSnapshot.enemyChampion != stats.enemyChampion ||
        g_state.csSnapshot.myPosition != stats.myPosition ||
        g_state.csSnapshot.enemyKnown != stats.enemyKnown;
    const bool intervalElapsed =
        !g_state.csSnapshot.valid ||
        now - g_state.csSnapshot.lastUpdate >= std::chrono::seconds(OverlayCsRefreshSeconds);

    if (!identityChanged && !intervalElapsed) {
        return false;
    }

    g_state.csSnapshot.valid = true;
    g_state.csSnapshot.enemyKnown = stats.enemyKnown;
    g_state.csSnapshot.myCreepScore = stats.myCreepScore;
    g_state.csSnapshot.enemyCreepScore = stats.enemyCreepScore;
    g_state.csSnapshot.myCsPerMinute = stats.myCsPerMinute;
    g_state.csSnapshot.enemyCsPerMinute = stats.enemyCsPerMinute;
    g_state.csSnapshot.myChampion = stats.myChampion;
    g_state.csSnapshot.enemyChampion = stats.enemyChampion;
    g_state.csSnapshot.myPosition = stats.myPosition;
    g_state.csSnapshot.lastUpdate = now;
    return true;
}

void resetScoreboardHome() {
    if (!scoreboardWindow()) {
        return;
    }
    if (g_state.guiPage != GuiPage::Home || g_state.selectedMatch.isObject()) {
        g_state.guiPage = GuiPage::Home;
        g_state.selectedMatch = Json::Value();
        g_state.selectedReport = Json::Value();
        g_state.selectedHistoryIndex = -1;
        g_state.historyScroll = 0;
        g_state.detailScroll = 0;
        resetHistoryPaging();
        g_state.homeHistoryLoaded = false;
    }
}

void pollLiveData() {
    if (g_state.movingWindow) {
        return;
    }

    if (!g_state.startScoreboard && !leagueSurfaceFocused()) {
        PerformanceTracker::observeGameUnavailable();
        updateOverlayCsSnapshot({});
        g_state.champSelect = {};
        hideOverlay();
        return;
    }

    const bool wasShowingLiveScreen = g_state.stats.visible || g_state.champSelect.visible;
    LaneOverlayStats nextStats;
    if (!fetchOverlayStats(nextStats) || !nextStats.visible) {
        PerformanceTracker::observeGameUnavailable();
        updateOverlayCsSnapshot({});
        ChampSelectState nextChampSelect;
        const auto now = std::chrono::steady_clock::now();
        const bool shouldPollChampSelect =
            !scoreboardWindow() ||
            g_state.lastChampSelectPoll.time_since_epoch().count() == 0 ||
            now - g_state.lastChampSelectPoll >= std::chrono::seconds(3);
        if (shouldPollChampSelect) {
            g_state.lastChampSelectPoll = now;
        }
        if (shouldPollChampSelect && pollChampSelectState(nextChampSelect, true)) {
            g_state.champSelect = nextChampSelect;
            g_state.stats = {};
            showOverlay();
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
        } else {
            g_state.champSelect = {};
            if (scoreboardWindow()) {
                if (wasShowingLiveScreen) {
                    resetScoreboardHome();
                }
                g_state.stats = {};
                showOverlay();
                InvalidateRect(g_state.hwnd, nullptr, FALSE);
            } else {
                hideOverlay();
            }
        }
        return;
    }

    const bool changed = nextStats != g_state.stats;
    g_state.stats = nextStats;
    g_state.champSelect = {};
    const bool csDisplayChanged = updateOverlayCsSnapshot(g_state.stats);
    showOverlay();
    refreshMetaForSelf(g_state.stats);

    if (changed || csDisplayChanged) {
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

void refreshMetaForSelf(const LaneOverlayStats& stats) {
    if (!scoreboardWindow() || !stats.visible) {
        return;
    }

    const MetaTarget target = metaTargetForSelf(stats);
    if (!target.ok) {
        const auto now = std::chrono::steady_clock::now();
        if (g_state.metaKey != "waiting-for-matchup" ||
            now - g_state.lastMetaFetch > std::chrono::seconds(10)) {
            g_state.metaKey = "waiting-for-matchup";
            g_state.lastMetaFetch = now;
            g_state.metaLines = {L"Waiting for your lane matchup from the live scoreboard..."};
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
        }
        return;
    }

    const std::string myChampion = normalizeOpggChampion(target.myChampion);
    const std::string enemyChampion = normalizeOpggChampion(target.enemyChampion);
    const std::string position = target.opggPosition;
    if (myChampion.empty() || enemyChampion.empty()) {
        return;
    }

    const std::string key = myChampion + "|" + enemyChampion + "|" + position;
    const auto now = std::chrono::steady_clock::now();
    if (key == g_state.metaKey && !g_state.metaLines.empty() &&
        now - g_state.lastMetaFetch < std::chrono::minutes(10)) {
        return;
    }

    g_state.metaKey = key;
    g_state.lastMetaFetch = now;
    g_state.metaLines = {L"Loading OP.GG recommendations for " + utf8ToWide(target.myChampion) +
                         L" vs " + utf8ToWide(target.enemyChampion) + L"..."};
    InvalidateRect(g_state.hwnd, nullptr, FALSE);

    std::ostringstream body;
    body << R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"lol_get_lane_matchup_guide","arguments":{)"
         << R"("position":")" << jsonEscape(position) << R"(",)"
         << R"("my_champion":")" << jsonEscape(myChampion) << R"(",)"
         << R"("opponent_champion":")" << jsonEscape(enemyChampion) << R"(",)"
         << R"("lang":"en_US"}}})";

    const HttpResponse response = ApiCaller::getInstance()->postJson("https://mcp-api.op.gg/mcp", body.str());
    if (!response.ok) {
        g_state.metaLines = {L"OP.GG unavailable: " + utf8ToWide(response.error.empty() ? "request failed" : response.error)};
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }

    Json::Value root;
    std::string parseError;
    if (!parseApiJson(response.body, root, parseError)) {
        g_state.metaLines = {L"OP.GG returned invalid data."};
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }

    if (root["error"].isObject()) {
        const std::string message = root["error"]["message"].asString();
        g_state.metaLines = {L"OP.GG error: " + utf8ToWide(message.empty() ? "matchup guide unavailable" : message)};
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }

    std::string text = root["result"]["content"][0]["text"].asString();
    Json::Value meta;
    if (!parseApiJson(text, meta, parseError)) {
        g_state.metaLines = {L"OP.GG matchup guide is not available for this lane."};
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }

    const Json::Value& data = meta["data"];
    std::vector<std::wstring> lines;
    lines.push_back(L"YOU: " + utf8ToWide(target.myChampion) + L" " + utf8ToWide(target.positionLabel) +
                    L" vs " + utf8ToWide(target.enemyChampion));

    if (data["starter_items"].isArray() && !data["starter_items"].empty()) {
        lines.push_back(L"Start: " + joinJsonStringArray(data["starter_items"][0]["ids_names"], 5));
    }
    if (data["boots"].isArray() && !data["boots"].empty()) {
        lines.push_back(L"Boots: " + joinJsonStringArray(data["boots"][0]["ids_names"], 2));
    }
    if (data["core_items"].isArray() && !data["core_items"].empty()) {
        lines.push_back(L"Core: " + joinJsonStringArray(data["core_items"][0]["ids_names"], 4));
    }
    if (data["runes"].isArray() && !data["runes"].empty()) {
        const Json::Value& runes = data["runes"][0];
        lines.push_back(L"Runes: " + utf8ToWide(runes["primary_page_name"].asString()) + L" [" +
                        joinJsonStringArray(runes["primary_rune_names"], 4) + L"]");
        lines.push_back(L"Secondary: " + utf8ToWide(runes["secondary_page_name"].asString()) + L" [" +
                        joinJsonStringArray(runes["secondary_rune_names"], 3) + L"]");
    }
    if (data["summoner_spells"].isArray() && !data["summoner_spells"].empty()) {
        lines.push_back(L"Spells: " + joinJsonStringArray(data["summoner_spells"][0]["ids_names"], 2));
    }
    if (data["skills"].isArray() && !data["skills"].empty()) {
        lines.push_back(L"Skill order: " + joinJsonStringArray(data["skills"][0]["order"], 8));
    }
    if (data["recommended_play_style"].isString()) {
        lines.push_back(L"Matchup style: " + utf8ToWide(data["recommended_play_style"].asString()));
    }
    if (data["lane_advantage_champion"].isString()) {
        lines.push_back(L"Lane edge: " + utf8ToWide(data["lane_advantage_champion"].asString()));
    }

    if (lines.size() == 1) {
        lines.push_back(L"No OP.GG build guide returned for this matchup yet.");
    }
    g_state.metaLines = lines;
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void drawText(HDC hdc, RECT rect, const std::wstring& text, const COLORREF color, const bool title = false,
              const UINT align = DT_LEFT) {
    SelectObject(hdc, title ? g_state.titleFont : g_state.font);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text.c_str(), -1, &rect, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void drawPanel(HDC hdc, const RECT& rect, const COLORREF fill, const COLORREF border = BorderColor) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawModeButton(HDC hdc) {
    const RECT rect = modeButtonRect(g_state.displayMode);
    drawPanel(hdc, rect, RGB(32, 38, 46), RGB(92, 105, 118));
    drawText(hdc, rect, modeButtonText(g_state.displayMode), GoldColor, false, DT_CENTER);
}

void drawStatPair(HDC hdc, int x, int y, const std::wstring& label, const std::wstring& value, COLORREF valueColor) {
    drawText(hdc, RECT{x, y, x + 34, y + 15}, label, MutedColor);
    drawText(hdc, RECT{x + 38, y, x + 136, y + 15}, value, valueColor, false, DT_RIGHT);
}

int displayedMyCs(const LaneOverlayStats& stats) {
    return g_state.csSnapshot.valid ? g_state.csSnapshot.myCreepScore : stats.myCreepScore;
}

int displayedEnemyCs(const LaneOverlayStats& stats) {
    return g_state.csSnapshot.valid ? g_state.csSnapshot.enemyCreepScore : stats.enemyCreepScore;
}

double displayedMyCsPerMinute(const LaneOverlayStats& stats) {
    return g_state.csSnapshot.valid ? g_state.csSnapshot.myCsPerMinute : stats.myCsPerMinute;
}

double displayedEnemyCsPerMinute(const LaneOverlayStats& stats) {
    return g_state.csSnapshot.valid ? g_state.csSnapshot.enemyCsPerMinute : stats.enemyCsPerMinute;
}

std::wstring sampledCsText(const int cs, const double csPerMinute) {
    return std::to_wstring(cs) + L" | " + formatDouble(csPerMinute) + L"/m";
}

void drawTableCell(HDC hdc, const RECT& rect, const std::wstring& text, const COLORREF color, const UINT align = DT_LEFT) {
    drawText(hdc, rect, text, color, false, align);
}

void drawScoreboardTeam(HDC hdc, const LaneOverlayStats& stats, const RECT& rect, const bool blueTeam) {
    drawPanel(hdc, rect, blueTeam ? PanelColor : PanelSoftColor, blueTeam ? AllyColor : EnemyColor);
    const COLORREF sideColor = blueTeam ? AllyColor : EnemyColor;
    const int inv = blueTeam ? stats.blueInventoryGold : stats.redInventoryGold;
    const int cs = blueTeam ? stats.blueCreepScore : stats.redCreepScore;
    const int vision = blueTeam ? stats.blueVisionScore : stats.redVisionScore;
    const int kills = blueTeam ? stats.blueKills : stats.redKills;
    const int deaths = blueTeam ? stats.blueDeaths : stats.redDeaths;
    const int assists = blueTeam ? stats.blueAssists : stats.redAssists;

    const int x = rect.left + 10;
    int y = rect.top + 7;
    drawText(hdc, RECT{x, y, rect.right - 10, y + 18},
             blueTeam ? L"BLUE TEAM" : L"RED TEAM", sideColor, true);
    y += 20;
    drawText(hdc, RECT{x, y, rect.right - 10, y + 16},
             L"Inv " + formatGold(inv) + L"   CS " + std::to_wstring(cs) +
             L"   Vis " + std::to_wstring(vision) + L"   KDA " + kdaText(kills, deaths, assists),
             TextColor);
    y += 22;

    const int roleX = x;
    const int champX = x + 34;
    const int playerX = x + 112;
    const int goldX = rect.right - 208;
    const int csX = rect.right - 144;
    const int visX = rect.right - 100;
    const int kdaX = rect.right - 58;

    drawTableCell(hdc, RECT{roleX, y, champX - 4, y + 15}, L"R", MutedColor);
    drawTableCell(hdc, RECT{champX, y, playerX - 6, y + 15}, L"Champion", sideColor);
    drawTableCell(hdc, RECT{playerX, y, goldX - 6, y + 15}, L"Player", TextColor);
    drawTableCell(hdc, RECT{goldX, y, csX - 6, y + 15}, L"Gold", GoldColor, DT_RIGHT);
    drawTableCell(hdc, RECT{csX, y, visX - 6, y + 15}, L"CS", AllyColor, DT_RIGHT);
    drawTableCell(hdc, RECT{visX, y, kdaX - 6, y + 15}, L"Vis", VisionColor, DT_RIGHT);
    drawTableCell(hdc, RECT{kdaX, y, rect.right - 10, y + 15}, L"KDA", GoodColor, DT_RIGHT);
    y += 17;

    for (const RoleInventoryRow& row : stats.roleInventoryRows) {
        if (y + 15 > rect.bottom - 7) {
            break;
        }
        const std::wstring champion = compactLabel(blueTeam ? row.blueChampion : row.redChampion, 10);
        const std::wstring playerName = compactLabel(blueTeam ? row.bluePlayerName : row.redPlayerName, 18);
        const int rowInv = blueTeam ? row.blueInventoryGold : row.redInventoryGold;
        const int rowCs = blueTeam ? row.blueCreepScore : row.redCreepScore;
        const int rowVision = blueTeam ? row.blueVisionScore : row.redVisionScore;
        const int rowKills = blueTeam ? row.blueKills : row.redKills;
        const int rowDeaths = blueTeam ? row.blueDeaths : row.redDeaths;
        const int rowAssists = blueTeam ? row.blueAssists : row.redAssists;

        drawTableCell(hdc, RECT{roleX, y, champX - 4, y + 15}, utf8ToWide(row.role), MutedColor);
        drawTableCell(hdc, RECT{champX, y, playerX - 6, y + 15}, champion, sideColor);
        drawTableCell(hdc, RECT{playerX, y, goldX - 6, y + 15}, playerName, TextColor);
        drawTableCell(hdc, RECT{goldX, y, csX - 6, y + 15}, formatGold(rowInv), GoldColor, DT_RIGHT);
        drawTableCell(hdc, RECT{csX, y, visX - 6, y + 15}, std::to_wstring(rowCs), TextColor, DT_RIGHT);
        drawTableCell(hdc, RECT{visX, y, kdaX - 6, y + 15}, std::to_wstring(rowVision), VisionColor, DT_RIGHT);
        drawTableCell(hdc, RECT{kdaX, y, rect.right - 10, y + 15}, kdaText(rowKills, rowDeaths, rowAssists), GoodColor, DT_RIGHT);
        y += 16;
    }
}

void drawScoreboardGui(HDC hdc, const LaneOverlayStats& stats, const RECT& rect) {
    drawPanel(hdc, rect, PanelColor);
    const int x = rect.left + 10;
    int y = rect.top + 7;
    drawText(hdc, RECT{x, y, rect.right - 58, y + 18}, L"GUI SCOREBOARD", GoldColor, true);
    drawModeButton(hdc);

    const int diff = stats.blueInventoryGold - stats.redInventoryGold;
    y += 20;
    drawText(hdc, RECT{x, y, rect.right - 10, y + 16},
             L"Blue " + formatGold(stats.blueInventoryGold) + L"   Red " + formatGold(stats.redInventoryGold) +
             L"   Edge " + formatSignedGold(diff),
             MutedColor);

    const int innerTop = y + 23;
    const int gap = 8;
    const int innerLeft = rect.left + 8;
    const int innerRight = rect.right - 8;
    const int metaHeight = std::max<int>(150, (rect.bottom - innerTop) / 3);
    const int tableBottom = rect.bottom - metaHeight - gap;
    const int innerBottom = std::max(innerTop + 130, tableBottom);
    const int width = innerRight - innerLeft;
    const bool sideBySide = width >= 860;
    if (sideBySide) {
        const int columnWidth = (width - gap) / 2;
        drawScoreboardTeam(hdc, stats, RECT{innerLeft, innerTop, innerLeft + columnWidth, innerBottom}, true);
        drawScoreboardTeam(hdc, stats, RECT{innerLeft + columnWidth + gap, innerTop, innerRight, innerBottom}, false);
    } else {
        const int height = (innerBottom - innerTop - gap) / 2;
        drawScoreboardTeam(hdc, stats, RECT{innerLeft, innerTop, innerRight, innerTop + height}, true);
        drawScoreboardTeam(hdc, stats, RECT{innerLeft, innerTop + height + gap, innerRight, innerBottom}, false);
    }

    const RECT metaRect{innerLeft, innerBottom + gap, innerRight, rect.bottom - 8};
    drawPanel(hdc, metaRect, RGB(12, 21, 28), GoldColor);
    int metaY = metaRect.top + 8;
    drawText(hdc, RECT{metaRect.left + 10, metaY, metaRect.right - 10, metaY + 18},
             L"YOUR RECOMMENDED BUILD / RUNES / MATCHUP", GoldColor, true);
    metaY += 22;

    if (g_state.metaLines.empty()) {
        drawText(hdc, RECT{metaRect.left + 10, metaY, metaRect.right - 10, metaY + 18},
                 L"Waiting for OP.GG matchup data for your champion...", MutedColor);
    } else {
        for (const std::wstring& line : g_state.metaLines) {
            if (metaY + 16 > metaRect.bottom - 6) {
                break;
            }
            drawText(hdc, RECT{metaRect.left + 10, metaY, metaRect.right - 10, metaY + 16},
                     line, TextColor);
            metaY += 17;
        }
    }
}

void drawScoreboardWaiting(HDC hdc, const RECT& rect) {
    drawPanel(hdc, rect, PanelColor);
    const int x = rect.left + 14;
    int y = rect.top + 12;
    drawText(hdc, RECT{x, y, rect.right - 58, y + 20}, L"GUI SCOREBOARD", GoldColor, true);
    drawModeButton(hdc);
    y += 32;
    drawText(hdc, RECT{x, y, rect.right - 14, y + 18}, L"Waiting for an active League match...", TextColor, true);
    y += 24;
    drawText(hdc, RECT{x, y, rect.right - 14, y + 18},
             L"Using only https://127.0.0.1:2999/liveclientdata/allgamedata", MutedColor);
    y += 20;
    drawText(hdc, RECT{x, y, rect.right - 14, y + 18},
             L"Click TUI to switch back to the terminal view.", MutedColor);
}

std::wstring historyKdaText(const MatchHistorySummary& summary) {
    return kdaText(summary.kills, summary.deaths, summary.assists);
}

std::wstring signedPlain(const int value) {
    if (value > 0) {
        return L"+" + std::to_wstring(value);
    }
    if (value < 0) {
        return L"-" + std::to_wstring(std::abs(value));
    }
    return L"even";
}

std::wstring signedNumberText(const int value, const std::wstring& suffix = L"") {
    if (value > 0) {
        return L"+" + std::to_wstring(value) + suffix;
    }
    if (value < 0) {
        return L"-" + std::to_wstring(std::abs(value)) + suffix;
    }
    return L"even";
}

std::wstring scoreText(const MatchHistorySummary& summary) {
    if (summary.grade == "Not scored") {
        return L"Not scored";
    }
    return utf8ToWide(std::to_string(summary.performanceScore) + " / " + summary.grade);
}

std::wstring gradeForDashboardScore(const int score) {
    if (score >= 97) return L"S+";
    if (score >= 93) return L"S";
    if (score >= 90) return L"S-";
    if (score >= 86) return L"A+";
    if (score >= 82) return L"A";
    if (score >= 78) return L"A-";
    if (score >= 74) return L"B+";
    if (score >= 70) return L"B";
    if (score >= 66) return L"B-";
    if (score >= 62) return L"C+";
    if (score >= 58) return L"C";
    if (score >= 52) return L"D";
    return L"D-";
}

void drawDashboardCard(HDC hdc, const RECT& rect, const std::wstring& title, const COLORREF border = BorderColor) {
    drawPanel(hdc, rect, PanelSoftColor, border);
    drawText(hdc, RECT{rect.left + 10, rect.top + 8, rect.right - 10, rect.top + 26}, title, GoldColor, true);
}

void drawCardLine(HDC hdc, int& y, const RECT& rect, const std::wstring& label,
                  const std::wstring& value, const COLORREF valueColor = TextColor) {
    drawText(hdc, RECT{rect.left + 10, y, rect.left + 130, y + 16}, label, MutedColor);
    drawText(hdc, RECT{rect.left + 134, y, rect.right - 10, y + 16}, value, valueColor);
    y += 17;
}

std::wstring championRoleText(const MatchHistorySummary& summary) {
    return utf8ToWide(summary.champion + " " + summary.role);
}

void drawLastGameCard(HDC hdc, const RECT& rect) {
    drawDashboardCard(hdc, rect, L"LAST GAME", GoldColor);
    int y = rect.top + 30;
    if (g_state.homeHistory.empty()) {
        drawText(hdc, RECT{rect.left + 10, y, rect.right - 10, y + 18}, L"No last game available.", MutedColor);
        g_state.lastReportButtonRect = RECT{0, 0, 0, 0};
        return;
    }

    const MatchHistorySummary& last = g_state.homeHistory.front();
    drawText(hdc, RECT{rect.left + 10, y, rect.right - 10, y + 18},
             championRoleText(last) + L" vs " + utf8ToWide(last.enemyChampion), AllyColor, true);
    y += 20;
    drawText(hdc, RECT{rect.left + 10, y, rect.right - 10, y + 16},
             utf8ToWide(MatchHistory::formatDuration(last.durationSeconds)) + L"   " +
             utf8ToWide(MatchHistory::formatDateTime(last.startedAtLocal)), MutedColor);
    y += 20;
    drawCardLine(hdc, y, rect, L"OL Score", scoreText(last), GoldColor);
    drawCardLine(hdc, y, rect, L"KDA", historyKdaText(last), GoodColor);
    drawCardLine(hdc, y, rect, L"CS", std::to_wstring(last.creepScore) + L" (" + formatDouble(last.csPerMinute, 2) + L"/m)", TextColor);
    drawCardLine(hdc, y, rect, L"Vision", std::to_wstring(last.visionScore), VisionColor);
    drawCardLine(hdc, y, rect, L"Lane", formatSignedGold(last.laneGoldDiff) + L" / " + signedNumberText(last.laneCsDiff, L" CS"),
                 last.laneGoldDiff >= 0 ? GoodColor : EnemyColor);

    g_state.lastReportButtonRect = RECT{rect.right - 118, rect.bottom - 30, rect.right - 10, rect.bottom - 8};
    drawPanel(hdc, g_state.lastReportButtonRect, RGB(32, 38, 46), GoldColor);
    drawText(hdc, g_state.lastReportButtonRect, L"Open Report", GoldColor, false, DT_CENTER);
}

void drawRecentMatchesCard(HDC hdc, const RECT& rect) {
    drawDashboardCard(hdc, rect, L"RECENT MATCHES");
    g_state.homeRecentRects.clear();
    int y = rect.top + 31;
    if (g_state.homeHistory.empty()) {
        drawText(hdc, RECT{rect.left + 10, y, rect.right - 10, y + 18},
                 L"Play a match with the app running to generate reports.", MutedColor);
        return;
    }

    const size_t shown = std::min<size_t>(5, g_state.homeHistory.size());
    for (size_t i = 0; i < shown; ++i) {
        const MatchHistorySummary& match = g_state.homeHistory[i];
        RECT row{rect.left + 8, y, rect.right - 8, y + 25};
        g_state.homeRecentRects.push_back(row);
        drawPanel(hdc, row, RGB(9, 14, 20), BorderColor);
        const int left = row.left + 8;
        drawText(hdc, RECT{left, y + 5, left + 145, y + 21}, championRoleText(match), AllyColor);
        drawText(hdc, RECT{left + 150, y + 5, left + 205, y + 21},
                 utf8ToWide(MatchHistory::formatDuration(match.durationSeconds)), MutedColor);
        drawText(hdc, RECT{left + 210, y + 5, left + 275, y + 21}, scoreText(match), GoldColor);
        drawText(hdc, RECT{left + 280, y + 5, left + 345, y + 21}, historyKdaText(match), GoodColor);
        drawText(hdc, RECT{left + 350, y + 5, left + 430, y + 21}, formatDouble(match.csPerMinute, 2) + L" CS/m", TextColor);
        drawText(hdc, RECT{left + 435, y + 5, row.right - 8, y + 21}, formatSignedGold(match.laneGoldDiff),
                 match.laneGoldDiff >= 0 ? GoodColor : EnemyColor, false, DT_RIGHT);
        y += 29;
    }
}

void drawLocalStatsCard(HDC hdc, const RECT& rect) {
    drawDashboardCard(hdc, rect, L"LOCAL STATS");
    int y = rect.top + 31;
    const int games = static_cast<int>(g_state.homeHistory.size());
    if (games == 0) {
        drawCardLine(hdc, y, rect, L"Games saved", std::to_wstring(g_state.savedMatchCount));
        drawCardLine(hdc, y, rect, L"Avg Score", L"-");
        drawCardLine(hdc, y, rect, L"Avg KDA", L"0 / 0 / 0");
        drawCardLine(hdc, y, rect, L"Avg CS/min", L"0.00");
        drawCardLine(hdc, y, rect, L"Avg Vision", L"0");
        return;
    }

    double score = 0.0;
    double kills = 0.0;
    double deaths = 0.0;
    double assists = 0.0;
    double csMin = 0.0;
    double vision = 0.0;
    int scoredGames = 0;
    std::map<std::string, int> champions;
    std::map<std::string, int> roles;
    for (const MatchHistorySummary& match : g_state.homeHistory) {
        if (match.grade != "Not scored") {
            score += match.performanceScore;
            ++scoredGames;
        }
        kills += match.kills;
        deaths += match.deaths;
        assists += match.assists;
        csMin += match.csPerMinute;
        vision += match.visionScore;
        champions[match.champion]++;
        roles[match.role]++;
    }

    const auto mostCommon = [](const std::map<std::string, int>& values) {
        std::string best = "-";
        int bestCount = -1;
        for (const auto& [name, count] : values) {
            if (count > bestCount) {
                best = name;
                bestCount = count;
            }
        }
        return best;
    };

    const int avgScore = scoredGames > 0 ? static_cast<int>(std::round(score / scoredGames)) : 0;
    drawCardLine(hdc, y, rect, L"Games saved", std::to_wstring(g_state.savedMatchCount));
    drawCardLine(hdc, y, rect, L"Avg OL Score", scoredGames > 0 ? std::to_wstring(avgScore) + L" / " + gradeForDashboardScore(avgScore) : L"-", GoldColor);
    drawCardLine(hdc, y, rect, L"Avg KDA",
                 formatDouble(kills / games, 1) + L" / " + formatDouble(deaths / games, 1) + L" / " + formatDouble(assists / games, 1),
                 GoodColor);
    drawCardLine(hdc, y, rect, L"Avg CS/min", formatDouble(csMin / games, 2));
    drawCardLine(hdc, y, rect, L"Avg Vision", formatDouble(vision / games, 1), VisionColor);
    drawCardLine(hdc, y, rect, L"Most played", utf8ToWide(mostCommon(champions)), AllyColor);
    drawCardLine(hdc, y, rect, L"Main role", utf8ToWide(mostCommon(roles)), AllyColor);
}

void drawStatusCard(HDC hdc, const RECT& rect) {
    drawDashboardCard(hdc, rect, L"STATUS");
    int y = rect.top + 31;
    drawCardLine(hdc, y, rect, L"League match", L"Not detected");
    drawCardLine(hdc, y, rect, L"Live API", L"Waiting");
    drawCardLine(hdc, y, rect, L"Overlay", L"Ready", GoodColor);
    drawCardLine(hdc, y, rect, L"Snapshots", L"Enabled", GoodColor);
    drawCardLine(hdc, y, rect, L"Events", L"Enabled", GoodColor);
    drawCardLine(hdc, y, rect, L"Saved matches", std::to_wstring(g_state.savedMatchCount), GoldColor);
    drawCardLine(hdc, y, rect, L"Start with Windows", g_state.settings.startWithWindows ? L"On" : L"Off",
                 g_state.settings.startWithWindows ? GoodColor : MutedColor);
    g_state.startupToggleRect = RECT{rect.right - 92, rect.bottom - 30, rect.right - 10, rect.bottom - 8};
    drawPanel(hdc, g_state.startupToggleRect, RGB(32, 38, 46), BorderColor);
    drawText(hdc, g_state.startupToggleRect, g_state.settings.startWithWindows ? L"Disable" : L"Enable",
             GoldColor, false, DT_CENTER);
}

void drawHomeGui(HDC hdc, const RECT& rect) {
    refreshHomeHistory();
    drawPanel(hdc, rect, PanelColor);
    g_state.homeRecentRects.clear();
    const int x = rect.left + 20;
    int y = rect.top + 18;
    drawText(hdc, RECT{x, y, rect.right - 58, y + 22}, L"LOL OVERLAY HOME", GoldColor, true);
    drawModeButton(hdc);
    y += 34;
    drawText(hdc, RECT{x, y, rect.right - 20, y + 20}, L"Not in an active League match.", TextColor, true);
    y += 26;
    drawText(hdc, RECT{x, y, rect.right - 20, y + 18},
             L"Your live scoreboard appears automatically in game. Local reports are available below.", MutedColor);

    const int top = y + 34;
    const int bottom = rect.bottom - 14;
    const int gap = 10;
    const bool wide = (rect.right - rect.left) >= 860;
    const int contentRight = rect.right - 20;
    const int leftWidth = wide ? ((contentRight - x - gap) * 52 / 100) : (contentRight - x);
    const int rightLeft = wide ? x + leftWidth + gap : x;
    const int rightWidth = wide ? (contentRight - rightLeft) : (contentRight - x);

    g_state.historyButtonRect = RECT{x, top, x + leftWidth, top + 62};
    drawPanel(hdc, g_state.historyButtonRect, PanelSoftColor, GoldColor);
    drawText(hdc, RECT{g_state.historyButtonRect.left + 16, g_state.historyButtonRect.top + 10,
                       g_state.historyButtonRect.right - 16, g_state.historyButtonRect.top + 30},
             L"See your player history", GoldColor, true);
    drawText(hdc, RECT{g_state.historyButtonRect.left + 16, g_state.historyButtonRect.top + 32,
                       g_state.historyButtonRect.right - 16, g_state.historyButtonRect.top + 50},
             L"Match history and reports", TextColor);

    if (wide) {
        drawRecentMatchesCard(hdc, RECT{x, top + 72, x + leftWidth, bottom});
        drawLastGameCard(hdc, RECT{rightLeft, top, rightLeft + rightWidth, top + 184});
        drawLocalStatsCard(hdc, RECT{rightLeft, top + 194, rightLeft + rightWidth, top + 350});
        drawStatusCard(hdc, RECT{rightLeft, top + 360, rightLeft + rightWidth, bottom});
    } else {
        int cardY = top + 72;
        drawLastGameCard(hdc, RECT{x, cardY, contentRight, cardY + 184});
        cardY += 194;
        drawRecentMatchesCard(hdc, RECT{x, cardY, contentRight, cardY + 186});
        cardY += 196;
        drawLocalStatsCard(hdc, RECT{x, cardY, contentRight, cardY + 156});
        cardY += 166;
        drawStatusCard(hdc, RECT{x, cardY, contentRight, std::min(bottom, cardY + 138)});
    }
}

void drawHistoryCard(HDC hdc, const MatchHistorySummary& summary, const RECT& rect) {
    drawPanel(hdc, rect, PanelSoftColor, BorderColor);
    const int x = rect.left + 12;
    int y = rect.top + 8;
    drawText(hdc, RECT{x, y, x + 210, y + 18},
             utf8ToWide(summary.champion + "  " + summary.role), AllyColor, true);
    drawText(hdc, RECT{rect.right - 120, y, rect.right - 12, y + 18},
             utf8ToWide(summary.grade + " / " + std::to_string(summary.performanceScore)),
             GoldColor, true, DT_RIGHT);
    y += 21;
    drawText(hdc, RECT{x, y, x + 170, y + 16},
             L"KDA " + historyKdaText(summary), TextColor);
    drawText(hdc, RECT{x + 180, y, x + 340, y + 16},
             L"CS " + std::to_wstring(summary.creepScore) + L" (" + formatDouble(summary.csPerMinute, 2) + L"/m)",
             TextColor);
    drawText(hdc, RECT{x + 350, y, rect.right - 12, y + 16},
             utf8ToWide(MatchHistory::formatDateTime(summary.startedAtLocal)), MutedColor, false, DT_RIGHT);
    y += 18;
    drawText(hdc, RECT{x, y, x + 150, y + 16},
             L"Vision " + std::to_wstring(summary.visionScore), VisionColor);
    drawText(hdc, RECT{x + 160, y, x + 320, y + 16},
             L"Inv " + formatGold(summary.inventoryGold), GoldColor);
    drawText(hdc, RECT{x + 330, y, rect.right - 12, y + 16},
             L"Duration " + utf8ToWide(MatchHistory::formatDuration(summary.durationSeconds)),
             MutedColor, false, DT_RIGHT);
    y += 18;
    drawText(hdc, RECT{x, y, rect.right - 12, y + 16},
             L"Vs " + utf8ToWide(summary.enemyChampion) + L"   Lane: " + utf8ToWide(summary.laneResult),
             summary.laneResult == "Won lane" ? GoodColor : (summary.laneResult == "Lost lane" ? EnemyColor : MutedColor));
}

void drawHistoryGui(HDC hdc, const RECT& rect) {
    refreshHistory();
    drawPanel(hdc, rect, PanelColor);
    g_state.historyCardRects.clear();

    const int x = rect.left + 16;
    int y = rect.top + 12;
    g_state.backButtonRect = RECT{x, y, x + 70, y + 24};
    drawPanel(hdc, g_state.backButtonRect, PanelSoftColor);
    drawText(hdc, g_state.backButtonRect, L"HOME", GoldColor, false, DT_CENTER);
    drawText(hdc, RECT{x + 86, y + 2, rect.right - 58, y + 22}, L"MATCH HISTORY", GoldColor, true);
    drawModeButton(hdc);

    y += 34;
    g_state.searchRect = RECT{x, y, rect.right - 16, y + 28};
    drawPanel(hdc, g_state.searchRect, RGB(9, 14, 20), BorderColor);
    const std::wstring filter = g_state.historyFilter.empty()
        ? L"Search champion, role, date, grade..."
        : utf8ToWide(g_state.historyFilter);
    drawText(hdc, RECT{g_state.searchRect.left + 10, y, g_state.searchRect.right - 10, y + 28},
             filter, g_state.historyFilter.empty() ? MutedColor : TextColor);

    y += 40;
    if (g_state.history.empty()) {
        drawText(hdc, RECT{x, y, rect.right - 16, y + 22},
                 L"No saved matches yet.", MutedColor, true);
        y += 24;
        drawText(hdc, RECT{x, y, rect.right - 16, y + 22},
                 L"Play a match with the app running to generate reports.", MutedColor);
        g_state.historyContentHeight = 0;
        g_state.historyScroll = 0;
        return;
    }

    const int cardHeight = 86;
    const int listTop = y;
    const int footerHeight = 34;
    g_state.historyContentHeight = static_cast<int>(g_state.history.size()) * (cardHeight + 8) + footerHeight;
    const int maxScroll = std::max(0, g_state.historyContentHeight - static_cast<int>(rect.bottom - listTop - 12));
    g_state.historyScroll = std::clamp(g_state.historyScroll, 0, maxScroll);
    y -= g_state.historyScroll;

    for (size_t i = 0; i < g_state.history.size(); ++i) {
        RECT card{x, y, rect.right - 16, y + cardHeight};
        if (card.bottom >= listTop && card.top <= rect.bottom - 12) {
            g_state.historyCardRects.push_back(card);
            drawHistoryCard(hdc, g_state.history[i], card);
        } else {
            g_state.historyCardRects.push_back(RECT{0, 0, 0, 0});
        }
        y += cardHeight + 8;
    }

    const RECT footer{x, y + 2, rect.right - 16, y + 26};
    if (footer.bottom >= listTop && footer.top <= rect.bottom - 12) {
        const std::wstring footerText = g_state.historyLoadingMore
            ? L"Loading more matches..."
            : (g_state.historyHasMore ? L"Scroll for more matches" : L"No more matches");
        drawText(hdc, footer, footerText, MutedColor, false, DT_CENTER);
    }

    if (maxScroll > 0) {
        drawText(hdc, RECT{rect.right - 150, rect.top + 48, rect.right - 16, rect.top + 66},
                 L"Mouse wheel to scroll", MutedColor, false, DT_RIGHT);
        const RECT track{rect.right - 8, listTop, rect.right - 4, rect.bottom - 12};
        HBRUSH trackBrush = CreateSolidBrush(RGB(36, 45, 54));
        FillRect(hdc, &track, trackBrush);
        DeleteObject(trackBrush);

        const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
        const int viewportHeight = std::max(1, static_cast<int>(rect.bottom - listTop - 12));
        const int thumbHeight = std::max(32, (viewportHeight * trackHeight) / std::max(viewportHeight, g_state.historyContentHeight));
        const int thumbTravel = std::max(1, trackHeight - thumbHeight);
        const int thumbTop = track.top + (g_state.historyScroll * thumbTravel) / maxScroll;
        const RECT thumb{track.left, thumbTop, track.right, thumbTop + thumbHeight};
        HBRUSH thumbBrush = CreateSolidBrush(RGB(92, 105, 118));
        FillRect(hdc, &thumb, thumbBrush);
        DeleteObject(thumbBrush);
    }
}

std::wstring playerBrief(const Json::Value& player) {
    return utf8ToWide(asString(player["champion"], "-")) + L" " +
           kdaText(asInt(player["kills"]), asInt(player["deaths"]), asInt(player["assists"])) +
           L"  CS " + std::to_wstring(asInt(player["creepScore"])) +
           L"  Vis " + std::to_wstring(asInt(player["visionScore"])) +
           L"  " + formatGold(asInt(player["inventoryGold"]));
}

int textPixelWidth(HDC hdc, const std::wstring& text) {
    SIZE size{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    return size.cx;
}

std::wstring roundedComponentText(const Json::Value& value, const int maxValue) {
    return std::to_wstring(static_cast<int>(std::round(asDouble(value)))) + L"/" + std::to_wstring(maxValue);
}

void drawComponentLine(HDC hdc, int& y, const RECT& rect, const std::wstring& label,
                       const Json::Value& value, const int maxValue) {
    drawText(hdc, RECT{rect.left, y, rect.left + 120, y + 16}, label, MutedColor);
    drawText(hdc, RECT{rect.left + 128, y, rect.right, y + 16},
             roundedComponentText(value, maxValue), GoodColor, false, DT_RIGHT);
    y += 17;
}

std::wstring joinJsonStrings(const Json::Value& values) {
    if (!values.isArray() || values.empty()) {
        return L"none";
    }
    std::wstring joined;
    for (Json::ArrayIndex i = 0; i < values.size(); ++i) {
        if (i > 0) {
            joined += L", ";
        }
        joined += utf8ToWide(asString(values[i]));
    }
    return joined;
}

const Json::Value* snapshotPlayer(const Json::Value& snapshot, const std::string& side) {
    const Json::Value& player = snapshot["matchup"][side];
    return player.isObject() ? &player : nullptr;
}

void drawLineChart(HDC hdc, const RECT& rect, const Json::Value& snapshots,
                   const std::wstring& title, const std::string& field) {
    drawPanel(hdc, rect, RGB(9, 14, 20), BorderColor);
    drawText(hdc, RECT{rect.left + 8, rect.top + 5, rect.right - 8, rect.top + 22}, title, GoldColor, true);

    if (!snapshots.isArray() || snapshots.size() < 2) {
        drawText(hdc, RECT{rect.left + 8, rect.top + 30, rect.right - 8, rect.top + 48},
                 L"Not enough snapshots for this graph.", MutedColor);
        return;
    }

    RECT plot{rect.left + 28, rect.top + 30, rect.right - 12, rect.bottom - 20};
    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(58, 72, 84));
    HGDIOBJ oldPen = SelectObject(hdc, axisPen);
    MoveToEx(hdc, plot.left, plot.bottom, nullptr);
    LineTo(hdc, plot.right, plot.bottom);
    MoveToEx(hdc, plot.left, plot.top, nullptr);
    LineTo(hdc, plot.left, plot.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(axisPen);

    double maxTime = 1.0;
    double maxValue = 1.0;
    for (const Json::Value& snapshot : snapshots) {
        maxTime = std::max(maxTime, asDouble(snapshot["timeSeconds"]));
        const Json::Value* me = snapshotPlayer(snapshot, "me");
        const Json::Value* enemy = snapshotPlayer(snapshot, "enemy");
        if (me) maxValue = std::max(maxValue, asDouble((*me)[field]));
        if (enemy) maxValue = std::max(maxValue, asDouble((*enemy)[field]));
    }

    const auto pointFor = [&](const Json::Value& snapshot, const Json::Value& player) {
        const double t = asDouble(snapshot["timeSeconds"]) / maxTime;
        const double v = asDouble(player[field]) / maxValue;
        return POINT{
            plot.left + static_cast<LONG>(t * (plot.right - plot.left)),
            plot.bottom - static_cast<LONG>(v * (plot.bottom - plot.top))
        };
    };

    const auto drawSeries = [&](const std::string& side, const COLORREF color) {
        HPEN pen = CreatePen(PS_SOLID, 2, color);
        HGDIOBJ previous = SelectObject(hdc, pen);
        bool started = false;
        for (const Json::Value& snapshot : snapshots) {
            const Json::Value* player = snapshotPlayer(snapshot, side);
            if (!player) {
                continue;
            }
            const POINT point = pointFor(snapshot, *player);
            if (!started) {
                MoveToEx(hdc, point.x, point.y, nullptr);
                started = true;
            } else {
                LineTo(hdc, point.x, point.y);
            }
        }
        SelectObject(hdc, previous);
        DeleteObject(pen);
    };

    drawSeries("me", AllyColor);
    drawSeries("enemy", EnemyColor);
    drawText(hdc, RECT{plot.left, rect.bottom - 18, plot.left + 120, rect.bottom - 4}, L"You", AllyColor);
    drawText(hdc, RECT{plot.left + 60, rect.bottom - 18, plot.left + 180, rect.bottom - 4}, L"Laner", EnemyColor);
}

void drawEventsList(HDC hdc, int& y, const RECT& rect, const Json::Value& events) {
    drawText(hdc, RECT{rect.left, y, rect.right, y + 18}, L"OBJECTIVES / EVENTS", GoldColor, true);
    y += 20;
    if (!events.isArray() || events.empty()) {
        drawText(hdc, RECT{rect.left, y, rect.right, y + 16}, L"No events recorded.", MutedColor);
        y += 18;
        return;
    }

    int shown = 0;
    for (Json::ArrayIndex i = 0; i < events.size() && shown < 9; ++i) {
        const Json::Value& event = events[events.size() - 1 - i];
        drawText(hdc, RECT{rect.left, y, rect.right, y + 16},
                 utf8ToWide(asString(event["timeDisplay"]) + "  " + asString(event["type"]) + "  " +
                            asString(event["killerName"], asString(event["victimName"]))),
                 TextColor);
        y += 16;
        ++shown;
    }
}

void drawFullScoreboardReport(HDC hdc, int& y, const RECT& rect, const Json::Value& players) {
    drawText(hdc, RECT{rect.left, y, rect.right, y + 18}, L"FULL SCOREBOARD", GoldColor, true);
    y += 20;
    int nameWidth = 78;
    const auto measureTeam = [&](const Json::Value& team) {
        for (const Json::Value& player : team) {
            nameWidth = std::max(nameWidth, textPixelWidth(hdc, compactLabel(asString(player["champion"], "-"), 18)));
        }
    };
    measureTeam(players["blue"]);
    measureTeam(players["red"]);
    nameWidth = std::min(nameWidth + 12, 150);

    const int nameX = rect.left;
    const int kdaX = nameX + nameWidth;
    const int csX = kdaX + 58;
    const int visionX = csX + 58;
    const int goldX = visionX + 58;

    const auto drawTeam = [&](const Json::Value& team, const COLORREF color) {
        for (const Json::Value& player : team) {
            if (y + 16 > rect.bottom - 4) {
                return;
            }
            drawText(hdc, RECT{nameX, y, kdaX - 4, y + 16}, compactLabel(asString(player["champion"], "-"), 18), color);
            drawText(hdc, RECT{kdaX, y, csX - 4, y + 16},
                     kdaText(asInt(player["kills"]), asInt(player["deaths"]), asInt(player["assists"])),
                     color);
            drawText(hdc, RECT{csX, y, visionX - 4, y + 16},
                     L"CS " + std::to_wstring(asInt(player["creepScore"])), color);
            drawText(hdc, RECT{visionX, y, goldX - 4, y + 16},
                     L"Vis " + std::to_wstring(asInt(player["visionScore"])), color);
            drawText(hdc, RECT{goldX, y, rect.right, y + 16}, formatGold(asInt(player["inventoryGold"])), color);
            y += 16;
        }
        y += 4;
    };
    drawTeam(players["blue"], AllyColor);
    drawTeam(players["red"], EnemyColor);
}

void drawDetailGui(HDC hdc, const RECT& rect) {
    drawPanel(hdc, rect, PanelColor);
    g_state.backButtonRect = RECT{rect.left + 16, rect.top + 12, rect.left + 92, rect.top + 36};
    drawPanel(hdc, g_state.backButtonRect, PanelSoftColor);
    drawText(hdc, g_state.backButtonRect, L"BACK", GoldColor, false, DT_CENTER);
    drawModeButton(hdc);

    if (!g_state.selectedMatch.isObject()) {
        drawText(hdc, RECT{rect.left + 16, rect.top + 58, rect.right - 16, rect.top + 78},
                 L"Could not load that match report.", EnemyColor, true);
        return;
    }

    HRGN clip = CreateRectRgn(rect.left, rect.top + 42, rect.right, rect.bottom);
    SelectClipRgn(hdc, clip);

    Json::Value& root = g_state.selectedMatch;
    const Json::Value report = MatchHistory::reportForMatch(root);
    const Json::Value& final = root["final"];
    const Json::Value& matchup = final["matchup"];
    const Json::Value& me = matchup["me"];
    const Json::Value& enemy = matchup["enemy"];
    const Json::Value& performance = report["performance"];
    const Json::Value& components = performance["components"];

    int y = rect.top + 48 - g_state.detailScroll;
    const int left = rect.left + 16;
    const int right = rect.right - 16;
    drawText(hdc, RECT{left, y, right, y + 22},
             utf8ToWide(asString(me["champion"]) + " " + asString(matchup["role"]) + " vs " +
                        asString(enemy["champion"], "unknown")),
             AllyColor, true);
    drawText(hdc, RECT{right - 170, y, right, y + 22},
             utf8ToWide(std::to_string(asInt(performance["score"])) + " / " + asString(performance["grade"], "NA")),
             GoldColor, true, DT_RIGHT);
    y += 26;
    drawText(hdc, RECT{left, y, right, y + 18},
             utf8ToWide("OL Score Performance Estimate: " + std::to_string(asInt(performance["score"])) + " / " +
                        asString(performance["grade"], "NA") + "   " + asString(report["summaryText"])),
             TextColor);
    y += 24;

    const int columnGap = 12;
    const int columnWidth = (right - left - columnGap) / 2;
    RECT laneRect{left, y, left + columnWidth, y + 150};
    RECT perfRect{left + columnWidth + columnGap, y, right, y + 150};
    drawPanel(hdc, laneRect, PanelSoftColor);
    drawPanel(hdc, perfRect, PanelSoftColor);

    int laneY = laneRect.top + 8;
    drawText(hdc, RECT{laneRect.left + 10, laneY, laneRect.right - 10, laneY + 18}, L"YOU VS LANER", GoldColor, true);
    laneY += 22;
    const auto deltaLine = [&](const std::wstring& label, const std::wstring& leftValue,
                               const std::wstring& rightValue, const std::wstring& delta) {
        drawText(hdc, RECT{laneRect.left + 10, laneY, laneRect.right - 10, laneY + 16},
                 label + L": " + leftValue + L" vs " + rightValue + L"  (" + delta + L")", TextColor);
        laneY += 17;
    };
    if (enemy.isObject()) {
        deltaLine(L"KDA", kdaText(asInt(me["kills"]), asInt(me["deaths"]), asInt(me["assists"])),
                  kdaText(asInt(enemy["kills"]), asInt(enemy["deaths"]), asInt(enemy["assists"])),
                  signedPlain(asInt(me["kills"]) - asInt(enemy["kills"])));
        deltaLine(L"CS", std::to_wstring(asInt(me["creepScore"])), std::to_wstring(asInt(enemy["creepScore"])),
                  signedPlain(asInt(me["creepScore"]) - asInt(enemy["creepScore"])));
        deltaLine(L"CS/m", formatDouble(asDouble(me["csPerMinute"]), 2), formatDouble(asDouble(enemy["csPerMinute"]), 2),
                  formatDouble(asDouble(me["csPerMinute"]) - asDouble(enemy["csPerMinute"]), 2));
        deltaLine(L"Vision", std::to_wstring(asInt(me["visionScore"])), std::to_wstring(asInt(enemy["visionScore"])),
                  signedPlain(asInt(me["visionScore"]) - asInt(enemy["visionScore"])));
        deltaLine(L"Inv Gold", formatGold(asInt(me["inventoryGold"])), formatGold(asInt(enemy["inventoryGold"])),
                  formatSignedGold(asInt(me["inventoryGold"]) - asInt(enemy["inventoryGold"])));
    } else {
        drawText(hdc, RECT{laneRect.left + 10, laneY, laneRect.right - 10, laneY + 16},
                 L"Enemy laner unavailable.", MutedColor);
    }

    int perfY = perfRect.top + 8;
    drawText(hdc, RECT{perfRect.left + 10, perfY, perfRect.right - 10, perfY + 18},
             L"OL SCORE BREAKDOWN", GoldColor, true);
    perfY += 22;
    RECT componentRect{perfRect.left + 10, perfY, perfRect.right - 10, perfRect.bottom - 6};
    const struct {
        const wchar_t* label;
        const char* key;
        int maxValue;
    } rows[] = {
        {L"KDA", "kdaScore", 18},
        {L"KP", "kpScore", 14},
        {L"Deaths", "deathScore", 14},
        {L"CS", "csScore", 16},
        {L"Gold", "goldScore", 16},
        {L"Vision", "visionScore", 10},
        {L"Lane", "laneScore", 12},
    };
    for (const auto& row : rows) {
        if (perfY + 17 > perfRect.bottom - 6) break;
        drawComponentLine(hdc, perfY, componentRect, row.label, components[row.key], row.maxValue);
    }
    if (perfY + 17 <= perfRect.bottom - 6) {
        drawText(hdc, RECT{componentRect.left, perfY, componentRect.right, perfY + 16},
                 L"Caps: " + joinJsonStrings(components["capsApplied"]), MutedColor);
        perfY += 17;
    }
    if (perfY + 17 <= perfRect.bottom - 6) {
        drawText(hdc, RECT{componentRect.left, perfY, componentRect.right, perfY + 16},
                 L"Bonus: " + joinJsonStrings(components["bonusesApplied"]), MutedColor);
    }

    y += 162;
    const Json::Value& teamDeltas = report["teamDeltas"];
    drawText(hdc, RECT{left, y, right, y + 18},
             L"TEAM DIFF  Blue kills " + signedPlain(asInt(teamDeltas["killsDiff"])) +
             L"   CS " + signedPlain(asInt(teamDeltas["csDiff"])) +
             L"   Vision " + signedPlain(asInt(teamDeltas["visionDiff"])) +
             L"   Inv " + formatSignedGold(asInt(teamDeltas["inventoryGoldDiff"])),
             MutedColor);
    y += 24;

    const int graphHeight = 126;
    drawLineChart(hdc, RECT{left, y, left + columnWidth, y + graphHeight}, root["snapshots"], L"Inventory Gold", "inventoryGold");
    drawLineChart(hdc, RECT{left + columnWidth + columnGap, y, right, y + graphHeight}, root["snapshots"], L"Creep Score", "creepScore");
    y += graphHeight + 10;
    drawLineChart(hdc, RECT{left, y, left + columnWidth, y + graphHeight}, root["snapshots"], L"Vision Score", "visionScore");
    drawLineChart(hdc, RECT{left + columnWidth + columnGap, y, right, y + graphHeight}, root["snapshots"], L"Kills", "kills");
    y += graphHeight + 14;

    RECT bottomLeft{left, y, left + columnWidth, rect.bottom - 12};
    RECT bottomRight{left + columnWidth + columnGap, y, right, rect.bottom - 12};
    int eventY = bottomLeft.top;
    drawEventsList(hdc, eventY, bottomLeft, root["events"]);
    int scoreboardY = bottomRight.top;
    drawFullScoreboardReport(hdc, scoreboardY, bottomRight, final["players"]);

    g_state.detailContentHeight = std::max(eventY, scoreboardY) - (rect.top + 48 - g_state.detailScroll) + 16;
    const int maxScroll = std::max(0, g_state.detailContentHeight - static_cast<int>(rect.bottom - rect.top - 58));
    g_state.detailScroll = std::clamp(g_state.detailScroll, 0, maxScroll);
    if (maxScroll > 0) {
        SelectClipRgn(hdc, nullptr);
        drawText(hdc, RECT{rect.right - 240, rect.top + 12, rect.right - 92, rect.top + 36},
                 L"Wheel to scroll", MutedColor, false, DT_RIGHT);
        const RECT track{rect.right - 8, rect.top + 44, rect.right - 4, rect.bottom - 10};
        HBRUSH trackBrush = CreateSolidBrush(RGB(36, 45, 54));
        FillRect(hdc, &track, trackBrush);
        DeleteObject(trackBrush);

        const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
        const int viewportHeight = std::max(1, static_cast<int>(rect.bottom - rect.top - 58));
        const int thumbHeight = std::max(28, (viewportHeight * trackHeight) / std::max(viewportHeight, g_state.detailContentHeight));
        const int thumbTravel = std::max(1, trackHeight - thumbHeight);
        const int thumbTop = track.top + (g_state.detailScroll * thumbTravel) / maxScroll;
        const RECT thumb{track.left, thumbTop, track.right, thumbTop + thumbHeight};
        HBRUSH thumbBrush = CreateSolidBrush(RGB(92, 105, 118));
        FillRect(hdc, &thumb, thumbBrush);
        DeleteObject(thumbBrush);
    } else {
        SelectClipRgn(hdc, nullptr);
    }
    DeleteObject(clip);
}

void drawGoldEdgeHeader(HDC hdc, const int y) {
    drawTableCell(hdc, RECT{12, y, 44, y + 15}, L"ROLE", MutedColor);
    drawTableCell(hdc, RECT{48, y, 148, y + 15}, L"ALLY", AllyColor);
    drawTableCell(hdc, RECT{154, y, 220, y + 15}, L"EDGE", GoldColor, DT_CENTER);
    drawTableCell(hdc, RECT{226, y, 358, y + 15}, L"ENEMY", EnemyColor, DT_RIGHT);
}

void drawGoldEdgeRow(HDC hdc, int y, const RoleInventoryRow& row, const bool myBlue) {
    const std::wstring allyChampion = championLabel(myBlue ? row.blueChampion : row.redChampion);
    const std::wstring enemyChampion = championLabel(myBlue ? row.redChampion : row.blueChampion);
    const int allyInv = myBlue ? row.blueInventoryGold : row.redInventoryGold;
    const int enemyInv = myBlue ? row.redInventoryGold : row.blueInventoryGold;

    drawTableCell(hdc, RECT{12, y, 44, y + 15}, utf8ToWide(row.role), MutedColor);
    drawTableCell(hdc, RECT{48, y, 104, y + 15}, allyChampion, AllyColor);
    drawTableCell(hdc, RECT{106, y, 148, y + 15}, formatGold(allyInv), GoldColor, DT_RIGHT);
    drawTableCell(hdc, RECT{154, y, 220, y + 15}, formatSignedGold(allyInv - enemyInv), GoldColor, DT_CENTER);
    drawTableCell(hdc, RECT{226, y, 286, y + 15}, enemyChampion, EnemyColor);
    drawTableCell(hdc, RECT{288, y, 358, y + 15}, formatGold(enemyInv), GoldColor, DT_RIGHT);
}

void drawSelfCard(HDC hdc, const LaneOverlayStats& stats, const RECT& rect) {
    drawPanel(hdc, rect, PanelColor);
    drawText(hdc, RECT{rect.left + 8, rect.top + 5, rect.right - 54, rect.top + 22},
             L"YOU", GoldColor, true);
    drawModeButton(hdc);

    const int x = rect.left + 8;
    int y = rect.top + 25;
    drawText(hdc, RECT{x, y, rect.right - 8, y + 15},
             utf8ToWide(stats.myChampion) + L"  " + utf8ToWide(stats.myPosition), TextColor);
    y += 17;
    drawStatPair(hdc, x, y, L"Gold", formatGold(stats.myInventoryGold), GoldColor);
    y += 15;
    drawStatPair(hdc, x, y, L"CS", sampledCsText(displayedMyCs(stats), displayedMyCsPerMinute(stats)), AllyColor);
    y += 15;
    drawStatPair(hdc, x, y, L"Vis", std::to_wstring(stats.myVisionScore), VisionColor);
    y += 15;
    drawStatPair(hdc, x, y, L"KDA", kdaText(stats.myKills, stats.myDeaths, stats.myAssists), GoodColor);
}

void drawLaneCard(HDC hdc, const LaneOverlayStats& stats, const RECT& rect) {
    drawPanel(hdc, rect, PanelSoftColor);
    const int x = rect.left + 8;
    int y = rect.top + 5;

    const std::wstring title = stats.enemyKnown
        ? L"ENEMY " + utf8ToWide(stats.enemyPosition)
        : L"ENEMY";
    drawText(hdc, RECT{x, y, rect.right - 8, y + 17}, title, EnemyColor, true);
    y += 20;

    if (!stats.enemyKnown) {
        drawText(hdc, RECT{x, y, rect.right - 8, y + 15}, L"Enemy: unknown", TextColor);
        return;
    }

    drawText(hdc, RECT{x, y, rect.right - 8, y + 15}, utf8ToWide(stats.enemyChampion), TextColor);
    y += 17;
    drawStatPair(hdc, x, y, L"Inv", formatGold(stats.enemyInventoryGold), GoldColor);
    y += 15;
    drawStatPair(hdc, x, y, L"CS", sampledCsText(displayedEnemyCs(stats), displayedEnemyCsPerMinute(stats)), EnemyColor);
    y += 15;
    drawStatPair(hdc, x, y, L"Vis", std::to_wstring(stats.enemyVisionScore), VisionColor);
    y += 15;
    drawStatPair(hdc, x, y, L"KDA", kdaText(stats.enemyKills, stats.enemyDeaths, stats.enemyAssists), GoodColor);
}

void drawTeamOverviewCard(HDC hdc, const LaneOverlayStats& stats, const RECT& rect) {
    drawPanel(hdc, rect, PanelColor);
    const int x = rect.left + 8;
    int y = rect.top + 5;
    drawText(hdc, RECT{x, y, rect.right - 54, y + 17}, L"TEAM INVENTORY", GoldColor, true);
    drawModeButton(hdc);

    const bool myBlue = stats.myTeam == "ORDER";
    const int allyInv = myBlue ? stats.blueInventoryGold : stats.redInventoryGold;
    const int enemyInv = myBlue ? stats.redInventoryGold : stats.blueInventoryGold;
    const int allyCs = myBlue ? stats.blueCreepScore : stats.redCreepScore;
    const int enemyCs = myBlue ? stats.redCreepScore : stats.blueCreepScore;
    const int allyVision = myBlue ? stats.blueVisionScore : stats.redVisionScore;
    const int enemyVision = myBlue ? stats.redVisionScore : stats.blueVisionScore;
    const int allyKills = myBlue ? stats.blueKills : stats.redKills;
    const int allyDeaths = myBlue ? stats.blueDeaths : stats.redDeaths;
    const int allyAssists = myBlue ? stats.blueAssists : stats.redAssists;
    const int enemyKills = myBlue ? stats.redKills : stats.blueKills;
    const int enemyDeaths = myBlue ? stats.redDeaths : stats.blueDeaths;
    const int enemyAssists = myBlue ? stats.redAssists : stats.blueAssists;

    y += 22;
    drawText(hdc, RECT{x, y, x + 92, y + 15}, L"ALLY", AllyColor, true, DT_CENTER);
    drawText(hdc, RECT{x + 118, y, x + 222, y + 15}, L"EDGE", GoldColor, true, DT_CENTER);
    drawText(hdc, RECT{x + 250, y, rect.right - 8, y + 15}, L"ENEMY", EnemyColor, true, DT_CENTER);
    y += 16;
    drawText(hdc, RECT{x, y, x + 92, y + 15}, formatGold(allyInv), TextColor, false, DT_CENTER);
    drawText(hdc, RECT{x + 118, y, x + 222, y + 15}, formatSignedGold(allyInv - enemyInv), GoldColor, false, DT_CENTER);
    drawText(hdc, RECT{x + 250, y, rect.right - 8, y + 15}, formatGold(enemyInv), TextColor, false, DT_CENTER);

    y += 18;
    drawText(hdc, RECT{x, y, rect.right - 8, y + 15},
             L"Team CS " + std::to_wstring(allyCs) + L" vs " + std::to_wstring(enemyCs) +
             L"   Vision " + std::to_wstring(allyVision) + L" vs " + std::to_wstring(enemyVision),
             MutedColor);
    y += 15;
    drawText(hdc, RECT{x, y, rect.right - 8, y + 15},
             L"KDA " + kdaText(allyKills, allyDeaths, allyAssists) + L" vs " +
             kdaText(enemyKills, enemyDeaths, enemyAssists),
             MutedColor);

    y += 19;
    drawGoldEdgeHeader(hdc, y);
    y += 16;
    for (const RoleInventoryRow& row : stats.roleInventoryRows) {
        if (y + 15 > rect.bottom - 6) {
            break;
        }
        drawGoldEdgeRow(hdc, y, row, myBlue);
        y += 15;
    }
}

std::wstring formatPercent(const double value) {
    if (value < 0.0) {
        return L"NA";
    }
    return formatDouble(value, 1) + L"%";
}

void drawChampSelectPanel(HDC hdc, const ChampSelectState& state, const RECT& rect) {
    drawPanel(hdc, rect, PanelColor, GoldColor);
    const int x = rect.left + 10;
    int y = rect.top + 7;
    drawText(hdc, RECT{x, y, rect.right - 10, y + 18}, L"CHAMP SELECT", GoldColor, true);
    y += 21;

    const std::wstring lockText = state.myChampionLocked ? L"LOCKED" : L"not locked";
    drawText(hdc, RECT{x, y, rect.right - 10, y + 16},
             L"You: " + utf8ToWide(state.myChampionName.empty() ? "not selected" : state.myChampionName) +
             L"  " + utf8ToWide(state.myPosition.empty() ? "ANY" : state.myPosition) +
             L"  " + lockText,
             TextColor);
    y += 17;
    drawText(hdc, RECT{x, y, rect.right - 10, y + 16},
             L"Runes: " + utf8ToWide(state.runeStatus.empty() ? "waiting" : state.runeStatus),
             VisionColor);
    y += 17;

    for (const std::string& line : state.runeLines) {
        if (y + 16 > rect.bottom - 70) {
            break;
        }
        drawText(hdc, RECT{x, y, rect.right - 10, y + 16}, utf8ToWide(line), MutedColor);
        y += 16;
    }

    drawText(hdc, RECT{x, y, rect.right - 10, y + 16},
             L"Items: " + utf8ToWide(state.itemSetStatus.empty() ? "waiting" : state.itemSetStatus),
             GoldColor);
    y += 17;

    for (const std::string& line : state.itemSetLines) {
        if (y + 16 > rect.bottom - 52) {
            break;
        }
        drawText(hdc, RECT{x, y, rect.right - 10, y + 16}, utf8ToWide(line), MutedColor);
        y += 16;
    }

    y += 4;
    drawText(hdc, RECT{x, y, rect.right - 10, y + 16}, L"ENEMY PICKS / COUNTER WR", EnemyColor, true);
    y += 18;

    if (state.enemyCounters.empty()) {
        drawText(hdc, RECT{x, y, rect.right - 10, y + 16},
                 L"Waiting for visible enemy picks...", MutedColor);
        return;
    }

    const int champX = x;
    const int relationX = x + 118;
    const int wrX = rect.right - 88;
    drawText(hdc, RECT{champX, y, relationX - 4, y + 15}, L"Champion", EnemyColor);
    drawText(hdc, RECT{relationX, y, wrX - 4, y + 15}, L"Matchup", MutedColor);
    drawText(hdc, RECT{wrX, y, rect.right - 10, y + 15}, L"Agg WR", GoldColor, false, DT_RIGHT);
    y += 16;

    for (const EnemyChampionCounter& enemy : state.enemyCounters) {
        if (y + 15 > rect.bottom - 8) {
            break;
        }
        drawText(hdc, RECT{champX, y, relationX - 4, y + 15}, compactLabel(enemy.championName, 14), EnemyColor);
        drawText(hdc, RECT{relationX, y, wrX - 4, y + 15}, utf8ToWide(enemy.relation), TextColor);
        drawText(hdc, RECT{wrX, y, rect.right - 10, y + 15}, formatPercent(enemy.myWinRate), GoldColor, false, DT_RIGHT);
        y += 15;
    }
}

void paintOverlay(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);
    const OverlaySize size = overlaySizeForMode(activeDisplayMode());
    HDC bufferDc = CreateCompatibleDC(hdc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(hdc, size.width, size.height);
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bufferBitmap);

    HBRUSH transparentBrush = CreateSolidBrush(TransparentColor);
    RECT fullRect{0, 0, size.width, size.height};
    FillRect(bufferDc, &fullRect, transparentBrush);
    DeleteObject(transparentBrush);

    SetBkMode(bufferDc, TRANSPARENT);

    const LaneOverlayStats& stats = g_state.stats;

    if (g_state.champSelect.visible) {
        drawChampSelectPanel(bufferDc, g_state.champSelect, RECT{0, 0, size.width, size.height});
    } else if (g_state.displayMode == DisplayMode::Scoreboard) {
        if (stats.visible) {
            drawScoreboardGui(bufferDc, stats, RECT{0, 0, size.width, size.height});
        } else if (g_state.guiPage == GuiPage::History) {
            drawHistoryGui(bufferDc, RECT{0, 0, size.width, size.height});
        } else if (g_state.guiPage == GuiPage::Detail) {
            drawDetailGui(bufferDc, RECT{0, 0, size.width, size.height});
        } else {
            drawHomeGui(bufferDc, RECT{0, 0, size.width, size.height});
        }
    } else if (g_state.displayMode == DisplayMode::FullTeams) {
        drawTeamOverviewCard(bufferDc, stats, RECT{0, 0, size.width, size.height - 4});
    } else if (g_state.displayMode == DisplayMode::LaneOnly) {
        drawSelfCard(bufferDc, stats, RECT{0, 0, size.width, 108});
        drawLaneCard(bufferDc, stats, RECT{0, 114, size.width, size.height - 4});
    } else {
        drawSelfCard(bufferDc, stats, RECT{0, 0, size.width, size.height - 4});
    }

    BitBlt(hdc, 0, 0, size.width, size.height, bufferDc, 0, 0, SRCCOPY);
    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bufferBitmap);
    DeleteDC(bufferDc);
    EndPaint(hwnd, &ps);
}

void cycleDisplayMode() {
    g_state.displayMode = nextDisplayMode(g_state.displayMode);
    g_state.config.mode = static_cast<int>(g_state.displayMode);
    resizeOverlay();
    saveConfig();
}

void nudgeOverlay(const int dx, const int dy) {
    g_state.config.x += dx;
    g_state.config.y += dy;
    resizeOverlay();
    saveConfig();
}

void launchTuiAndExit() {
    const std::filesystem::path tuiPath = moduleDirectory() / "LOL_overlay.exe";
    if (std::filesystem::exists(tuiPath)) {
        std::wstring commandLine = L"\"" + tuiPath.wstring() + L"\" --no-gui-overlay";
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (CreateProcessW(tuiPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                           CREATE_NEW_CONSOLE, nullptr, tuiPath.parent_path().c_str(),
                           &startupInfo, &processInfo)) {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
        }
    }

    DestroyWindow(g_state.hwnd);
}

void handleModeButtonClick() {
    if (g_state.displayMode == DisplayMode::Scoreboard) {
        launchTuiAndExit();
    } else {
        cycleDisplayMode();
    }
}

bool openMatchReport(const MatchHistorySummary& summary) {
    Json::Value root;
    if (!MatchHistory::loadMatchFile(summary.path, root)) {
        return false;
    }
    g_state.selectedMatch = root;
    g_state.selectedReport = MatchHistory::reportForMatch(g_state.selectedMatch);
    g_state.detailScroll = 0;
    g_state.guiPage = GuiPage::Detail;
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
    return true;
}

bool openHistoryMatch(const size_t index) {
    if (index >= g_state.history.size()) {
        return false;
    }
    g_state.selectedHistoryIndex = static_cast<int>(index);
    return openMatchReport(g_state.history[index]);
}

bool openHomeMatch(const size_t index) {
    if (index >= g_state.homeHistory.size()) {
        return false;
    }
    g_state.selectedHistoryIndex = -1;
    return openMatchReport(g_state.homeHistory[index]);
}

void handleScoreboardGuiClick(const POINT point) {
    if (g_state.stats.visible || g_state.champSelect.visible || g_state.displayMode != DisplayMode::Scoreboard) {
        return;
    }

    if (g_state.guiPage == GuiPage::Home) {
        if (pointInRect(point, g_state.historyButtonRect)) {
            g_state.guiPage = GuiPage::History;
            g_state.historyScroll = 0;
            refreshHistory(true);
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            return;
        }
        if (pointInRect(point, g_state.lastReportButtonRect)) {
            openHomeMatch(0);
            return;
        }
        if (pointInRect(point, g_state.startupToggleRect)) {
            toggleStartWithWindows();
            return;
        }
        for (size_t i = 0; i < g_state.homeRecentRects.size(); ++i) {
            if (pointInRect(point, g_state.homeRecentRects[i])) {
                openHomeMatch(i);
                return;
            }
        }
        return;
    }

    if (g_state.guiPage == GuiPage::History) {
        if (pointInRect(point, g_state.backButtonRect)) {
            g_state.guiPage = GuiPage::Home;
            g_state.historyScroll = 0;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            return;
        }
        for (size_t i = 0; i < g_state.historyCardRects.size(); ++i) {
            if (pointInRect(point, g_state.historyCardRects[i])) {
                openHistoryMatch(i);
                return;
            }
        }
        return;
    }

    if (g_state.guiPage == GuiPage::Detail && pointInRect(point, g_state.backButtonRect)) {
        g_state.selectedMatch = Json::Value();
        g_state.selectedReport = Json::Value();
        g_state.selectedHistoryIndex = -1;
        g_state.detailScroll = 0;
        g_state.guiPage = GuiPage::History;
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

void handleMouseWheel(const int delta) {
    if (!scoreboardWindow() || g_state.stats.visible || g_state.champSelect.visible) {
        return;
    }

    const int amount = delta > 0 ? -96 : 96;
    const OverlaySize size = overlaySizeForMode(DisplayMode::Scoreboard);
    if (g_state.guiPage == GuiPage::History) {
        const int maxScroll = std::max(0, g_state.historyContentHeight - (size.height - 120));
        g_state.historyScroll = std::clamp(g_state.historyScroll + amount, 0, maxScroll);
        if (g_state.historyHasMore && maxScroll - g_state.historyScroll < 180) {
            loadMoreHistoryIfNeeded();
        }
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    } else if (g_state.guiPage == GuiPage::Detail) {
        const int maxScroll = std::max(0, g_state.detailContentHeight - (size.height - 58));
        g_state.detailScroll = std::clamp(g_state.detailScroll + amount, 0, maxScroll);
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

void handleHistoryKeyDown(const WPARAM wParam) {
    if (!scoreboardWindow() || g_state.stats.visible || g_state.champSelect.visible) {
        return;
    }

    const OverlaySize size = overlaySizeForMode(DisplayMode::Scoreboard);
    const int viewport = size.height - 120;
    int delta = 0;
    if (wParam == VK_UP) {
        delta = -32;
    } else if (wParam == VK_DOWN) {
        delta = 32;
    } else if (wParam == VK_PRIOR) {
        delta = -viewport;
    } else if (wParam == VK_NEXT) {
        delta = viewport;
    } else if (wParam == VK_HOME) {
        g_state.historyScroll = 0;
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    } else if (wParam == VK_END) {
        g_state.historyScroll = std::max(0, g_state.historyContentHeight - viewport);
        if (g_state.historyHasMore) {
            loadMoreHistoryIfNeeded(true);
        }
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }

    if (delta == 0) {
        return;
    }

    if (g_state.guiPage == GuiPage::History) {
        const int maxScroll = std::max(0, g_state.historyContentHeight - viewport);
        g_state.historyScroll = std::clamp(g_state.historyScroll + delta, 0, maxScroll);
        if (g_state.historyHasMore && maxScroll - g_state.historyScroll < 180) {
            loadMoreHistoryIfNeeded();
        }
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    } else if (g_state.guiPage == GuiPage::Detail) {
        const int maxScroll = std::max(0, g_state.detailContentHeight - (size.height - 58));
        g_state.detailScroll = std::clamp(g_state.detailScroll + delta, 0, maxScroll);
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

void handleHistoryChar(const WPARAM wParam) {
    if (!scoreboardWindow() || g_state.guiPage != GuiPage::History || g_state.stats.visible || g_state.champSelect.visible) {
        return;
    }
    if (wParam == VK_BACK) {
        if (!g_state.historyFilter.empty()) {
            g_state.historyFilter.pop_back();
            resetHistoryPaging();
            refreshHistory(true);
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
        }
        return;
    }
    if (wParam == VK_ESCAPE) {
        g_state.historyFilter.clear();
        resetHistoryPaging();
        refreshHistory(true);
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }
    if (wParam >= 32 && wParam < 127 && g_state.historyFilter.size() < 64) {
        g_state.historyFilter.push_back(static_cast<char>(wParam));
        resetHistoryPaging();
        refreshHistory(true);
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_state.hwnd = hwnd;
            AppPaths::ensureDataDirectories();
            AppPaths::migrateLegacyData(moduleDirectory());
            g_state.settings = AppSettingsStore::load();
            AppSettingsStore::syncStartupRegistry(g_state.settings);
            g_state.configPath = AppPaths::configPath(g_state.startScoreboard ? "scoreboard_config.ini" : "overlay_config.ini");
            g_state.config = loadConfig(g_state.configPath);
            if (g_state.startScoreboard) {
                g_state.displayMode = DisplayMode::Scoreboard;
            } else {
                const int overlayMode = (g_state.config.mode >= 0 && g_state.config.mode <= 2) ? g_state.config.mode : 0;
                g_state.displayMode = static_cast<DisplayMode>(overlayMode);
            }
            g_state.font = CreateFontW(scoreboardWindow() ? -14 : -12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_state.titleFont = CreateFontW(scoreboardWindow() ? -16 : -13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            if (!scoreboardWindow()) {
                SetLayeredWindowAttributes(hwnd, TransparentColor, OverlayAlpha, LWA_COLORKEY | LWA_ALPHA);
                RegisterHotKey(hwnd, HotkeyDisplayMode, 0, VK_F7);
                RegisterHotKey(hwnd, HotkeyNudgeLeft, MOD_CONTROL | MOD_ALT, VK_LEFT);
                RegisterHotKey(hwnd, HotkeyNudgeRight, MOD_CONTROL | MOD_ALT, VK_RIGHT);
                RegisterHotKey(hwnd, HotkeyNudgeUp, MOD_CONTROL | MOD_ALT, VK_UP);
                RegisterHotKey(hwnd, HotkeyNudgeDown, MOD_CONTROL | MOD_ALT, VK_DOWN);
            }
            SetTimer(hwnd, PollTimerId, PollIntervalMs, nullptr);
            {
                const OverlaySize size = windowSizeForMode(g_state.displayMode);
                if (scoreboardWindow()) {
                    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, size.width, size.height, SWP_NOMOVE);
                } else {
                    SetWindowPos(hwnd, HWND_TOPMOST, g_state.config.x, g_state.config.y,
                                 size.width, size.height, SWP_NOACTIVATE);
                }
            }
            PostMessageW(hwnd, WM_TIMER, PollTimerId, 0);
            if (scoreboardWindow()) {
                PostMessageW(hwnd, StartupPromptMessage, 0, 0);
            }
            return 0;

        case WM_TIMER:
            if (wParam == PollTimerId) {
                pollLiveData();
            }
            return 0;

        case WM_HOTKEY:
            if (wParam == HotkeyDisplayMode) {
                cycleDisplayMode();
            } else if (wParam == HotkeyNudgeLeft) {
                nudgeOverlay(-10, 0);
            } else if (wParam == HotkeyNudgeRight) {
                nudgeOverlay(10, 0);
            } else if (wParam == HotkeyNudgeUp) {
                nudgeOverlay(0, -10);
            } else if (wParam == HotkeyNudgeDown) {
                nudgeOverlay(0, 10);
            }
            return 0;

        case WM_NCHITTEST: {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT clientPoint = screenPoint;
            ScreenToClient(hwnd, &clientPoint);
            if (pointInRect(clientPoint, modeButtonRect(g_state.displayMode))) {
                return HTCLIENT;
            }
            return scoreboardWindow() ? HTCLIENT : HTCAPTION;
        }

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_LBUTTONDOWN:
            if (pointInRect(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, modeButtonRect(g_state.displayMode))) {
                handleModeButtonClick();
            } else if (scoreboardWindow()) {
                handleScoreboardGuiClick(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            } else if (!scoreboardWindow()) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;

        case WM_MOUSEWHEEL:
            handleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_KEYDOWN:
            handleHistoryKeyDown(wParam);
            return 0;

        case WM_CHAR:
            handleHistoryChar(wParam);
            return 0;

        case WM_ENTERSIZEMOVE:
            g_state.movingWindow = true;
            return 0;

        case WM_EXITSIZEMOVE:
            g_state.movingWindow = false;
            saveConfig();
            PostMessageW(hwnd, WM_TIMER, PollTimerId, 0);
            return 0;

        case ShowScoreboardMessage:
            g_state.displayMode = DisplayMode::Scoreboard;
            g_state.config.mode = static_cast<int>(g_state.displayMode);
            resizeOverlay();
            saveConfig();
            return 0;

        case StartupPromptMessage:
            promptStartWithWindowsIfNeeded();
            return 0;

        case WM_MOVE:
            {
                RECT rect{};
                if (GetWindowRect(hwnd, &rect)) {
                    g_state.config.x = rect.left;
                    g_state.config.y = rect.top;
                }
            }
            return 0;

        case WM_PAINT:
            paintOverlay(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            closeOverlayCompanionIfNeeded();
            saveConfig();
            UnregisterHotKey(hwnd, HotkeyDisplayMode);
            UnregisterHotKey(hwnd, HotkeyNudgeLeft);
            UnregisterHotKey(hwnd, HotkeyNudgeRight);
            UnregisterHotKey(hwnd, HotkeyNudgeUp);
            UnregisterHotKey(hwnd, HotkeyNudgeDown);
            KillTimer(hwnd, PollTimerId);
            if (g_state.font) {
                DeleteObject(g_state.font);
            }
            if (g_state.titleFont) {
                DeleteObject(g_state.titleFont);
            }
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR commandLine, int) {
    const std::string args = commandLine ? commandLine : "";
    g_state.startScoreboard = args.find("--scoreboard") != std::string::npos;

    const wchar_t* className = g_state.startScoreboard
        ? L"LOLScoreboardGuiWindow"
        : OverlayWindowClassName;
    const wchar_t* mutexName = g_state.startScoreboard
        ? L"LOLScoreboardGuiSingleton"
        : L"LOLOverlayLightweightGuiSingleton";

    HANDLE singleton = CreateMutexW(nullptr, TRUE, mutexName);
    if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(className, nullptr);
        if (existing) {
            if (g_state.startScoreboard) {
                ShowWindow(existing, SW_SHOWNORMAL);
                SetForegroundWindow(existing);
            } else {
                PostMessageW(existing, WM_TIMER, PollTimerId, 0);
            }
        }
        CloseHandle(singleton);
        return 0;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const DWORD exStyle = g_state.startScoreboard
        ? WS_EX_APPWINDOW
        : (WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    const DWORD style = g_state.startScoreboard
        ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)
        : WS_POPUP;
    const OverlaySize initialSize = overlaySizeForMode(g_state.startScoreboard ? DisplayMode::Scoreboard : DisplayMode::FullTeams);
    RECT windowRect{0, 0, initialSize.width, initialSize.height};
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
    HWND hwnd = CreateWindowExW(exStyle, className,
                                g_state.startScoreboard ? L"LOL GUI Scoreboard" : L"LOL Overlay",
                                style, CW_USEDEFAULT, CW_USEDEFAULT,
                                windowRect.right - windowRect.left,
                                windowRect.bottom - windowRect.top,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        return 1;
    }

    launchOverlayCompanionIfNeeded();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (singleton) {
        CloseHandle(singleton);
    }
    return static_cast<int>(message.wParam);
}
