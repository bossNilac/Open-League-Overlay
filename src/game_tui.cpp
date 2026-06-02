#include "game_tui.h"

#include "api/api_caller.h"
#include "api/json_parser.h"
#include "champ_select.h"
#include "item_catalog.h"
#include "performance_tracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#endif

namespace {
constexpr const char* ClearScreen = "\x1b[2J\x1b[H";
constexpr const char* ClearLine = "\x1b[2K";
constexpr const char* HideTerminalCursor = "\x1b[?25l";
constexpr const char* ShowTerminalCursor = "\x1b[?25h";
constexpr const char* Bold = "\x1b[1m";
constexpr const char* Dim = "\x1b[2m";
constexpr const char* Red = "\x1b[31m";
constexpr const char* Green = "\x1b[32m";
constexpr const char* Yellow = "\x1b[33m";
constexpr const char* Cyan = "\x1b[36m";
constexpr const char* Magenta = "\x1b[35m";
constexpr const char* Gold = "\x1b[38;5;220m";
constexpr const char* Reset = "\x1b[0m";

struct TerminalSize {
    int width = 120;
    int height = 40;
};

#ifdef _WIN32
struct ConsoleFontBackup {
    CONSOLE_FONT_INFOEX font{};
    bool captured = false;
    bool changed = false;
    int appliedHeight = 0;
};

ConsoleFontBackup g_consoleFontBackup;
#endif

void enableVirtualTerminal() {
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(output, &mode)) {
        return;
    }

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(output, mode);
#endif
}

#ifdef _WIN32
int suggestedConsoleFontHeightForMonitor() {
    HWND consoleWindow = GetConsoleWindow();
    HMONITOR monitor = consoleWindow
        ? MonitorFromWindow(consoleWindow, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTONEAREST);

    int workWidth = 1920;
    int workHeight = 1080;
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    }

    if (workHeight <= 800 || workWidth <= 1366) {
        return 10;
    }
    if (workHeight <= 950) {
        return 11;
    }
    if (workHeight <= 1100) {
        return 12;
    }
    if (workHeight <= 1250) {
        return 14;
    }
    return 16;
}

void applyConsoleFontHeight(const int requestedHeight, const bool allowAutoScale) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE) {
        return;
    }

    CONSOLE_FONT_INFOEX current{};
    current.cbSize = sizeof(current);
    if (!GetCurrentConsoleFontEx(output, FALSE, &current)) {
        return;
    }

    if (!g_consoleFontBackup.captured) {
        g_consoleFontBackup.font = current;
        g_consoleFontBackup.captured = true;
    }

    int targetHeight = requestedHeight;
    if (targetHeight <= 0 && allowAutoScale) {
        targetHeight = suggestedConsoleFontHeightForMonitor();
    }
    if (targetHeight <= 0) {
        return;
    }

    const int originalHeight = g_consoleFontBackup.font.dwFontSize.Y;
    const int desiredHeight = std::max(6, std::min(originalHeight, targetHeight));
    if (current.dwFontSize.Y == desiredHeight && g_consoleFontBackup.appliedHeight == desiredHeight) {
        return;
    }

    current.dwFontSize.Y = static_cast<SHORT>(desiredHeight);
    current.dwFontSize.X = 0;
    if (SetCurrentConsoleFontEx(output, FALSE, &current)) {
        g_consoleFontBackup.changed = true;
        g_consoleFontBackup.appliedHeight = desiredHeight;
    }
}

void restoreConsoleFont() {
    if (!g_consoleFontBackup.captured || !g_consoleFontBackup.changed) {
        return;
    }

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE) {
        return;
    }

    g_consoleFontBackup.font.cbSize = sizeof(g_consoleFontBackup.font);
    SetCurrentConsoleFontEx(output, FALSE, &g_consoleFontBackup.font);
}

BOOL WINAPI consoleControlHandler(DWORD controlType) {
    switch (controlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            std::cout << Reset << ShowTerminalCursor << std::flush;
            restoreConsoleFont();
            return FALSE;
        default:
            return FALSE;
    }
}
#endif

enum class InputAction {
    None,
    Quit,
    Gui,
    ToggleDetails,
    ToggleItems
};

InputAction readInputAction() {
#ifdef _WIN32
    if (_kbhit()) {
        const int ch = _getch();
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            return InputAction::Quit;
        }
        if (ch == 'g' || ch == 'G') {
            return InputAction::Gui;
        }
        if (ch == 'd' || ch == 'D') {
            return InputAction::ToggleDetails;
        }
        if (ch == 'i' || ch == 'I') {
            return InputAction::ToggleItems;
        }
    }
#endif
    return InputAction::None;
}

TerminalSize terminalSize() {
    TerminalSize size;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info)) {
        size.width = std::max<int>(40, info.srWindow.Right - info.srWindow.Left + 1);
        size.height = std::max<int>(12, info.srWindow.Bottom - info.srWindow.Top + 1);
    }
#endif
    return size;
}

std::vector<std::string> splitFrameLines(const std::string& frame) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream input(frame);
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool isAnsiFinalByte(const char ch) {
    return ch >= '@' && ch <= '~';
}

std::string clipAnsiLine(const std::string& line, const int terminalWidth) {
    const int visibleLimit = std::max(1, terminalWidth - 1);
    int visible = 0;
    std::string clipped;

    for (size_t i = 0; i < line.size();) {
        if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {
            const size_t start = i;
            i += 2;
            while (i < line.size() && !isAnsiFinalByte(line[i])) {
                ++i;
            }
            if (i < line.size()) {
                ++i;
            }
            clipped.append(line, start, i - start);
            continue;
        }

        if (visible >= visibleLimit) {
            break;
        }

        clipped.push_back(line[i]);
        ++i;
        ++visible;
    }

    clipped += Reset;
    return clipped;
}

int visibleLength(const std::string& line) {
    int visible = 0;
    for (size_t i = 0; i < line.size();) {
        if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {
            i += 2;
            while (i < line.size() && !isAnsiFinalByte(line[i])) {
                ++i;
            }
            if (i < line.size()) {
                ++i;
            }
            continue;
        }
        ++visible;
        ++i;
    }
    return visible;
}

std::string padVisible(std::string line, const int width) {
    const int missing = width - visibleLength(line);
    if (missing > 0) {
        line.append(static_cast<size_t>(missing), ' ');
    }
    return line;
}

std::string asString(const Json::Value& value, const std::string& fallback = "-") {
    if (value.isString()) {
        const std::string text = value.asString();
        return text.empty() ? fallback : text;
    }
    if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    }
    if (value.isNumeric()) {
        return value.asString();
    }
    return fallback;
}

int asInt(const Json::Value& value, const int fallback = 0) {
    return value.isNumeric() ? value.asInt() : fallback;
}

double asDouble(const Json::Value& value, const double fallback = 0.0) {
    return value.isNumeric() ? value.asDouble() : fallback;
}

bool asBool(const Json::Value& value, const bool fallback = false) {
    if (value.isBool()) {
        return value.asBool();
    }
    if (value.isString()) {
        const std::string text = value.asString();
        return text == "true" || text == "True" || text == "1";
    }
    return fallback;
}

bool containsText(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string truncateText(std::string text, const size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }
    if (maxLength <= 3) {
        return text.substr(0, maxLength);
    }
    return text.substr(0, maxLength - 3) + "...";
}

std::string formatGameTime(double seconds) {
    seconds = std::max(0.0, seconds);
    const int totalSeconds = static_cast<int>(std::floor(seconds));
    const int minutes = totalSeconds / 60;
    const int remainingSeconds = totalSeconds % 60;

    std::ostringstream out;
    out << minutes << ':' << std::setw(2) << std::setfill('0') << remainingSeconds;
    return out.str();
}

std::string formatRespawnSeconds(double seconds) {
    seconds = std::max(0.0, seconds);
    return std::to_string(static_cast<int>(std::ceil(seconds))) + "s";
}

std::string formatDouble(const double value, const int precision = 0) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string formatPercent(const double ratio, const int precision = 0) {
    return formatDouble(ratio * 100.0, precision) + "%";
}

std::string formatStatPercent(const double value, const int precision = 0) {
    const double percent = value > 1.0 ? value : value * 100.0;
    return formatDouble(percent, precision) + "%";
}

std::string formatPenetrationPercent(const double value, const int precision = 0) {
    const double percent = value >= 1.0 ? (value - 1.0) * 100.0 : value * 100.0;
    return formatDouble(std::max(0.0, percent), precision) + "%";
}

double gameMinutes(const double gameTime) {
    return std::max(gameTime / 60.0, 1.0 / 60.0);
}

std::string normalizeName(const std::string& text) {
    std::string normalized;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

void addIdentityKey(std::vector<std::string>& keys, const std::string& value) {
    if (value.empty() || value == "-") {
        return;
    }
    keys.push_back(value);
}

std::string playerName(const Json::Value& player) {
    const std::string riotId = asString(player["riotId"], "");
    if (!riotId.empty()) {
        return riotId;
    }

    const std::string gameName = asString(player["riotIdGameName"], "");
    const std::string tagLine = asString(player["riotIdTagLine"], "");
    if (!gameName.empty() && !tagLine.empty()) {
        return gameName + "#" + tagLine;
    }

    return asString(player["summonerName"]);
}

std::vector<std::string> identityKeys(const Json::Value& player) {
    std::vector<std::string> keys;
    addIdentityKey(keys, playerName(player));
    addIdentityKey(keys, asString(player["riotId"], ""));
    addIdentityKey(keys, asString(player["riotIdGameName"], ""));
    addIdentityKey(keys, asString(player["summonerName"], ""));
    addIdentityKey(keys, asString(player["championName"], ""));
    return keys;
}

bool sameIdentity(const Json::Value& left, const Json::Value& right) {
    const std::vector<std::string> leftKeys = identityKeys(left);
    const std::vector<std::string> rightKeys = identityKeys(right);

    for (const std::string& leftKey : leftKeys) {
        const std::string normalizedLeft = normalizeName(leftKey);
        if (normalizedLeft.empty()) {
            continue;
        }
        for (const std::string& rightKey : rightKeys) {
            if (normalizedLeft == normalizeName(rightKey)) {
                return true;
            }
        }
    }

    return false;
}

Json::Value findActiveRosterPlayer(const Json::Value& players, const Json::Value& activePlayer) {
    if (!players.isArray()) {
        return Json::Value();
    }

    for (const auto& player : players) {
        if (sameIdentity(player, activePlayer)) {
            return player;
        }
    }

    return Json::Value();
}

std::string teamDisplayName(const std::string& team) {
    if (team == "ORDER") {
        return "Blue";
    }
    if (team == "CHAOS") {
        return "Red";
    }
    return team.empty() ? "Unknown" : team;
}

const char* teamColor(const std::string& team) {
    return team == "ORDER" ? Cyan : Red;
}

std::string kdaText(const Json::Value& scores) {
    std::ostringstream out;
    out << asInt(scores["kills"]) << '/'
        << asInt(scores["deaths"]) << '/'
        << asInt(scores["assists"]);
    return out.str();
}

double kdaRatio(const Json::Value& scores) {
    const int kills = asInt(scores["kills"]);
    const int deaths = std::max(1, asInt(scores["deaths"]));
    const int assists = asInt(scores["assists"]);
    return static_cast<double>(kills + assists) / deaths;
}

double killParticipation(const Json::Value& scores, const int teamKills) {
    if (teamKills <= 0) {
        return 0.0;
    }
    return static_cast<double>(asInt(scores["kills"]) + asInt(scores["assists"])) / teamKills;
}

int positionRank(const std::string& position) {
    if (position == "TOP") return 0;
    if (position == "JUNGLE") return 1;
    if (position == "MIDDLE" || position == "MID") return 2;
    if (position == "BOTTOM" || position == "ADC") return 3;
    if (position == "UTILITY" || position == "SUPPORT") return 4;
    return 5;
}

std::string positionLabel(const Json::Value& player) {
    const std::string position = asString(player["position"], "");
    if (position == "TOP") return "TOP";
    if (position == "JUNGLE") return "JG";
    if (position == "MIDDLE" || position == "MID") return "MID";
    if (position == "BOTTOM" || position == "ADC") return "BOT";
    if (position == "UTILITY" || position == "SUPPORT") return "SUP";
    return "-";
}

std::vector<Json::Value> playersForTeam(const Json::Value& players, const std::string& team) {
    std::vector<Json::Value> result;
    if (!players.isArray()) {
        return result;
    }

    for (const auto& player : players) {
        if (asString(player["team"], "") == team) {
            result.push_back(player);
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const Json::Value& left, const Json::Value& right) {
        const std::string leftPosition = asString(left["position"], "");
        const std::string rightPosition = asString(right["position"], "");
        const int leftRank = positionRank(leftPosition);
        const int rightRank = positionRank(rightPosition);
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        return playerName(left) < playerName(right);
    });
    return result;
}

int itemValue(const Json::Value& item) {
    return ItemCatalog::itemValue(item);
}

int inventoryValue(const Json::Value& items) {
    return ItemCatalog::inventoryValue(items);
}

std::string itemSummary(const Json::Value& items) {
    std::vector<Json::Value> sortedItems;
    if (items.isArray()) {
        for (const auto& item : items) {
            sortedItems.push_back(item);
        }
    }

    std::stable_sort(sortedItems.begin(), sortedItems.end(), [](const Json::Value& left, const Json::Value& right) {
        return asInt(left["slot"], 99) < asInt(right["slot"], 99);
    });

    std::ostringstream out;
    bool first = true;
    for (const auto& item : sortedItems) {
        if (!first) {
            out << ", ";
        }
        first = false;

        const int count = std::max(1, asInt(item["count"], 1));
        out << asString(item["displayName"], "Item");
        if (count > 1) {
            out << 'x' << count;
        }
        out << " (" << itemValue(item) << "g)";
    }

    const std::string text = out.str();
    return text.empty() ? "-" : truncateText(text, 118);
}

struct TeamTotals {
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int creepScore = 0;
    double wardScore = 0.0;
    int inventory = 0;
};

TeamTotals totalsFor(const std::vector<Json::Value>& players) {
    TeamTotals totals;
    for (const auto& player : players) {
        const Json::Value& scores = player["scores"];
        totals.kills += asInt(scores["kills"]);
        totals.deaths += asInt(scores["deaths"]);
        totals.assists += asInt(scores["assists"]);
        totals.creepScore += asInt(scores["creepScore"]);
        totals.wardScore += asDouble(scores["wardScore"]);
        totals.inventory += inventoryValue(player["items"]);
    }
    return totals;
}

std::string spellSummary(const Json::Value& spells) {
    const std::string first = asString(spells["summonerSpellOne"]["displayName"], "");
    const std::string second = asString(spells["summonerSpellTwo"]["displayName"], "");
    if (first.empty() && second.empty()) {
        return "-";
    }
    if (first.empty()) {
        return second;
    }
    if (second.empty()) {
        return first;
    }
    return first + "/" + second;
}

std::string runeSummary(const Json::Value& runes) {
    const std::string keystone = asString(runes["keystone"]["displayName"], "");
    const std::string primary = asString(runes["primaryRuneTree"]["displayName"], "");
    const std::string secondary = asString(runes["secondaryRuneTree"]["displayName"], "");

    std::ostringstream out;
    if (!keystone.empty()) {
        out << keystone;
    }
    if (!primary.empty() || !secondary.empty()) {
        if (out.tellp() > 0) {
            out << " | ";
        }
        out << primary;
        if (!secondary.empty()) {
            out << " + " << secondary;
        }
    }

    const std::string text = out.str();
    return text.empty() ? "-" : text;
}

std::string statShardName(const std::string& rawDescription) {
    if (containsText(rawDescription, "Adaptive")) return "Adaptive";
    if (containsText(rawDescription, "AttackSpeed")) return "AtkSpd";
    if (containsText(rawDescription, "CooldownReduction")) return "Haste";
    if (containsText(rawDescription, "MagicResist")) return "MR";
    if (containsText(rawDescription, "Armor")) return "Armor";
    if (containsText(rawDescription, "HealthScaling")) return "HP/lvl";
    if (containsText(rawDescription, "Health")) return "HP";
    if (containsText(rawDescription, "MoveSpeed")) return "MS";
    if (containsText(rawDescription, "Tenacity")) return "Tenacity";
    return truncateText(rawDescription, 18);
}

std::string statShardSummary(const Json::Value& runes) {
    const Json::Value& statRunes = runes["statRunes"];
    if (!statRunes.isArray() || statRunes.empty()) {
        return "-";
    }

    std::ostringstream out;
    for (Json::ArrayIndex i = 0; i < statRunes.size(); ++i) {
        if (i > 0) {
            out << '/';
        }
        out << statShardName(asString(statRunes[i]["rawDescription"], ""));
    }
    return out.str();
}

double csPerMinute(const Json::Value& scores, const double gameTime) {
    return asDouble(scores["creepScore"]) / gameMinutes(gameTime);
}

double visionPerMinute(const Json::Value& scores, const double gameTime) {
    return asDouble(scores["wardScore"]) / gameMinutes(gameTime);
}

void renderPlayerRows(std::ostream& out,
                      const std::vector<Json::Value>& players,
                      const double gameTime,
                      const int teamKills,
                      const Json::Value& activePlayer,
                      const bool showItems) {
    out << "Role Champion       Lv  Player                 K/D/A     KDA    KP    CS CS/m  Vis V/m   Inv    Spells           Rune             Status\n";
    out << "---------------------------------------------------------------------------------------------------------------------------------------\n";

    for (const auto& player : players) {
        const Json::Value& scores = player["scores"];
        const bool dead = asBool(player["isDead"]);
        const bool self = sameIdentity(player, activePlayer);
        const std::string status = dead ? "Dead " + formatRespawnSeconds(asDouble(player["respawnTimer"])) : "Alive";
        const char* identityColor = self ? Gold : Reset;

        out << std::left << identityColor
                  << std::setw(5) << positionLabel(player)
                  << std::setw(15) << truncateText(asString(player["championName"]), 14)
                  << std::right << std::setw(2) << asInt(player["level"]) << Reset << "  "
                  << identityColor << std::left << std::setw(22) << truncateText(playerName(player), 21) << Reset
                  << Green << std::setw(8) << kdaText(scores) << Reset
                  << std::right << Magenta << std::setw(6) << formatDouble(kdaRatio(scores), 2) << Reset
                  << Gold << std::setw(6) << formatPercent(killParticipation(scores, teamKills)) << Reset
                  << Cyan << std::setw(6) << asInt(scores["creepScore"])
                  << std::setw(5) << formatDouble(csPerMinute(scores, gameTime), 1) << Reset
                  << Magenta << std::setw(6) << formatDouble(asDouble(scores["wardScore"]), 0)
                  << std::setw(5) << formatDouble(visionPerMinute(scores, gameTime), 1) << Reset
                  << Yellow << std::setw(7) << inventoryValue(player["items"]) << Reset
                  << "  "
                  << std::left << std::setw(17) << truncateText(spellSummary(player["summonerSpells"]), 16)
                  << std::setw(17) << truncateText(runeSummary(player["runes"]), 16)
                  << (dead ? Red : Dim) << status << Reset << '\n';

        if (showItems) {
            out << Dim << "     Items: " << Reset
                << (self ? Gold : Yellow) << itemSummary(player["items"]) << Reset << '\n';
        }
    }
}

void renderTeam(std::ostream& out,
                const std::string& team,
                const std::vector<Json::Value>& players,
                const double gameTime,
                const Json::Value& activePlayer,
                const bool showItems) {
    const TeamTotals totals = totalsFor(players);
    const double minutes = gameMinutes(gameTime);

    out << '\n' << teamColor(team) << Bold << teamDisplayName(team) << " Team"
              << Reset << Dim << " (" << team << ")" << Reset
              << "  K/D/A " << Green << totals.kills << '/' << totals.deaths << '/' << totals.assists << Reset
              << "  CS " << Cyan << totals.creepScore << " (" << formatDouble(totals.creepScore / minutes, 1) << "/m)" << Reset
              << "  Vision " << Magenta << formatDouble(totals.wardScore, 0) << " (" << formatDouble(totals.wardScore / minutes, 1) << "/m)" << Reset
              << "  Inventory " << Yellow << totals.inventory << "g" << Reset << '\n';
    renderPlayerRows(out, players, gameTime, totals.kills, activePlayer, showItems);
}

std::string abilityLine(const Json::Value& abilities) {
    const std::vector<std::string> keys = {"Q", "W", "E", "R"};
    std::ostringstream out;

    bool first = true;
    for (const std::string& key : keys) {
        const Json::Value& ability = abilities[key];
        if (ability.isNull()) {
            continue;
        }
        if (!first) {
            out << "  ";
        }
        first = false;
        out << key << ':' << asString(ability["displayName"], key)
            << " lvl " << asInt(ability["abilityLevel"]);
    }

    const std::string text = out.str();
    return text.empty() ? "-" : text;
}

std::string teamForParticipantName(const Json::Value& players, const std::string& participantName) {
    const std::string normalizedParticipant = normalizeName(participantName);
    if (normalizedParticipant.empty() || !players.isArray()) {
        return "";
    }

    for (const auto& player : players) {
        for (const std::string& key : identityKeys(player)) {
            if (normalizedParticipant == normalizeName(key)) {
                return asString(player["team"], "");
            }
        }
    }

    return "";
}

std::string objectiveTitle(const Json::Value& event) {
    const std::string name = asString(event["EventName"], "");
    if (name == "DragonKill") {
        const std::string dragonType = asString(event["DragonType"], "");
        return dragonType.empty() ? "Dragon" : dragonType + " Dragon";
    }
    if (name == "HeraldKill") return "Rift Herald";
    if (name == "HordeKill") return "Voidgrub";
    if (name == "BaronKill") return "Baron";
    if (name == "TurretKilled") return "Turret destroyed";
    if (name == "InhibKilled") return "Inhibitor destroyed";
    if (name == "InhibRespawningSoon") return "Inhibitor respawning soon";
    if (name == "InhibRespawned") return "Inhibitor respawned";
    if (name == "FirstBrick") return "First turret";
    return "";
}

std::string objectiveSide(const Json::Value& event, const Json::Value& players) {
    const std::string killer = asString(event["KillerName"], "");
    if (!killer.empty()) {
        return teamDisplayName(teamForParticipantName(players, killer));
    }

    const std::string acingTeam = asString(event["AcingTeam"], "");
    if (!acingTeam.empty()) {
        return teamDisplayName(acingTeam);
    }

    return "Unknown";
}

std::string eventTeam(const Json::Value& event, const Json::Value& players) {
    const std::string killer = asString(event["KillerName"], "");
    if (!killer.empty()) {
        const std::string team = teamForParticipantName(players, killer);
        if (!team.empty()) {
            return team;
        }
    }

    const std::string acingTeam = asString(event["AcingTeam"], "");
    if (!acingTeam.empty()) {
        return acingTeam;
    }

    return "Unknown";
}

std::string compactObjectiveLine(const Json::Value& event, const Json::Value& players) {
    const std::string name = asString(event["EventName"], "");
    const std::string team = eventTeam(event, players);
    std::ostringstream line;
    line << std::setw(6) << formatGameTime(asDouble(event["EventTime"])) << "  ";

    if (name == "DragonKill") {
        const std::string dragonType = asString(event["DragonType"], "");
        line << "Dragon - " << team;
        if (!dragonType.empty()) {
            line << " - " << dragonType;
        }
    } else if (name == "HeraldKill") {
        line << "Herald - " << team;
    } else if (name == "BaronKill") {
        line << "Baron - " << team;
    } else if (name == "TurretKilled") {
        line << "Tower - " << team << " - " << truncateText(asString(event["TurretKilled"], ""), 34);
    } else if (name == "InhibKilled") {
        line << "Inhib - " << team << " - " << truncateText(asString(event["InhibKilled"], ""), 34);
    } else if (name == "FirstBrick") {
        line << "FirstBrick - " << team;
    } else {
        line << name << " - " << team;
    }

    const std::string stolen = asString(event["Stolen"], "");
    if (stolen == "true" || stolen == "True") {
        line << " (stolen)";
    }
    return truncateText(line.str(), 120);
}

bool isObjectiveEvent(const std::string& name) {
    return name == "DragonKill" || name == "HeraldKill" || name == "BaronKill" ||
           name == "TurretKilled" || name == "InhibKilled" || name == "FirstBrick";
}

std::vector<std::string> objectiveEventLines(const Json::Value& events, const Json::Value& players, const int maxLines) {
    std::vector<std::string> objectiveLines;
    if (events.isArray()) {
        for (const auto& event : events) {
            const std::string name = asString(event["EventName"], "");
            if (!isObjectiveEvent(name)) {
                continue;
            }
            objectiveLines.push_back(compactObjectiveLine(event, players));
        }
    }

    if (objectiveLines.empty()) {
        return {"No objective events yet."};
    }

    const int count = std::min<int>(std::max(1, maxLines), static_cast<int>(objectiveLines.size()));
    const int start = static_cast<int>(objectiveLines.size()) - count;
    return std::vector<std::string>(objectiveLines.begin() + start, objectiveLines.end());
}

void renderObjectives(std::ostream& out, const Json::Value& events, const Json::Value& players, const int maxLines) {
    out << '\n' << Bold << "Objectives" << Reset << '\n';
    const std::vector<std::string> lines = objectiveEventLines(events, players, maxLines);
    for (const std::string& line : lines) {
        out << line << '\n';
    }
}

bool isRecentEvent(const std::string& name) {
    return name == "ChampionKill" || name == "FirstBlood" || name == "Multikill" ||
           name == "Ace" || name == "GameEnd";
}

std::string eventDetails(const Json::Value& event) {
    const std::string name = asString(event["EventName"]);
    const std::string time = formatGameTime(asDouble(event["EventTime"]));
    if (name == "ChampionKill" || name == "FirstBlood") {
        return truncateText(time + "  Kill - " + asString(event["KillerName"], "Unknown") +
                            " killed " + asString(event["VictimName"], "Unknown"), 120);
    }
    if (name == "Multikill") {
        return truncateText(time + "  Multikill - " + asString(event["KillerName"], "Unknown") +
                            " streak " + asString(event["KillStreak"], "?"), 120);
    }
    if (name == "Ace") {
        return truncateText(time + "  Ace - " + asString(event["AcingTeam"], "Unknown"), 120);
    }
    if (name == "GameEnd") {
        return truncateText(time + "  GameEnd - " + asString(event["Result"], "finished"), 120);
    }

    std::ostringstream out;
    out << time << "  " << name;

    const std::vector<std::string> detailKeys = {
        "KillerName", "VictimName", "DragonType", "Stolen", "Acer", "AcingTeam",
        "TurretKilled", "InhibKilled", "KillStreak"
    };

    bool hasDetail = false;
    for (const std::string& key : detailKeys) {
        if (!event.isMember(key)) {
            continue;
        }
        out << (hasDetail ? ", " : " - ") << key << ": " << asString(event[key]);
        hasDetail = true;
    }

    if (event.isMember("Assisters") && event["Assisters"].isArray() && !event["Assisters"].empty()) {
        out << (hasDetail ? ", " : " - ") << "Assists: ";
        for (Json::ArrayIndex i = 0; i < event["Assisters"].size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << asString(event["Assisters"][i]);
        }
    }

    return truncateText(out.str(), 120);
}

std::vector<std::string> recentEventLines(const Json::Value& events, const int maxLines) {
    std::vector<std::string> lines;
    if (events.isArray()) {
        for (const Json::Value& event : events) {
            if (isRecentEvent(asString(event["EventName"], ""))) {
                lines.push_back(eventDetails(event));
            }
        }
    }

    if (lines.empty()) {
        return {"No recent fight events yet."};
    }

    const int count = std::min<int>(std::max(1, maxLines), static_cast<int>(lines.size()));
    const int start = static_cast<int>(lines.size()) - count;
    return std::vector<std::string>(lines.begin() + start, lines.end());
}

void renderEvents(std::ostream& out, const Json::Value& events, const int maxLines) {
    out << '\n' << Bold << "Recent Events" << Reset << '\n';
    for (const std::string& line : recentEventLines(events, maxLines)) {
        out << line << '\n';
    }
}

void renderSidePanel(std::ostream& out, const std::string& teamBlock, const Json::Value& events,
                     const Json::Value& players, const int terminalWidth) {
    const int leftWidth = 137;
    const int sideWidth = std::max(28, terminalWidth - leftWidth - 2);
    std::vector<std::string> leftLines = splitFrameLines(teamBlock);
    std::vector<std::string> sideLines;
    sideLines.push_back(std::string(Bold) + "Objectives" + Reset);
    for (std::string line : objectiveEventLines(events, players, 8)) {
        sideLines.push_back(truncateText(std::move(line), static_cast<size_t>(sideWidth)));
    }
    sideLines.push_back("");
    sideLines.push_back(std::string(Bold) + "Recent Events" + Reset);
    for (std::string line : recentEventLines(events, 8)) {
        sideLines.push_back(truncateText(std::move(line), static_cast<size_t>(sideWidth)));
    }

    const size_t rows = std::max(leftLines.size(), sideLines.size());
    for (size_t i = 0; i < rows; ++i) {
        const std::string left = i < leftLines.size() ? leftLines[i] : "";
        const std::string side = i < sideLines.size() ? sideLines[i] : "";
        out << padVisible(left, leftWidth) << "  " << side << '\n';
    }
}

std::string inventoryDifferenceText(const int blueInventory, const int redInventory) {
    const int diff = blueInventory - redInventory;
    if (diff > 0) {
        return "Blue +" + std::to_string(diff) + "g";
    }
    if (diff < 0) {
        return "Red +" + std::to_string(std::abs(diff)) + "g";
    }
    return "Even";
}
}

GameTui::GameTui(const int refreshMs, const bool once, const int consoleFontHeight, const bool autoScaleConsoleFont)
    : refreshMs(refreshMs),
      once(once),
      consoleFontHeight(consoleFontHeight),
      autoScaleConsoleFont(autoScaleConsoleFont) {}

int GameTui::run() {
    enableVirtualTerminal();
#ifdef _WIN32
    applyConsoleFontHeight(consoleFontHeight, autoScaleConsoleFont);
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);
#endif

    do {
        Json::Value root;
        std::string error;

        if (pollGameData(root, error)) {
            PerformanceTracker::observeLiveGame(root);
            renderGame(root);
        } else {
            PerformanceTracker::observeGameUnavailable();
            ChampSelectState champSelect;
            if (pollChampSelectState(champSelect, true)) {
                renderChampSelect(champSelect);
            } else {
                renderWaiting(error);
            }
        }

        if (once) {
            break;
        }
    } while (waitForNextFrame());

    std::cout << Reset << ShowTerminalCursor << '\n';
#ifdef _WIN32
    restoreConsoleFont();
    SetConsoleCtrlHandler(consoleControlHandler, FALSE);
#endif
    return guiRequested ? 2 : 0;
}

bool GameTui::pollGameData(Json::Value& root, std::string& error) const {
    const HttpResponse response = ApiCaller::getInstance()->getLiveEndpoint("allgamedata");
    if (!response.ok) {
        error = response.error.empty() ? "No response from League game client" : response.error;
        return false;
    }

    std::string parseError;
    if (!parseApiJson(response.body, root, parseError)) {
        error = "Live Client returned invalid JSON: " + parseError;
        return false;
    }

    if (!root.isObject()) {
        error = "Live Client returned an unexpected payload";
        return false;
    }

    return true;
}

void GameTui::presentFrame(const std::string& frame) {
#ifdef _WIN32
    applyConsoleFontHeight(consoleFontHeight, autoScaleConsoleFont);
#endif
    const TerminalSize size = terminalSize();
    if (frame == lastFrame && size.width == lastTerminalWidth && size.height == lastTerminalHeight) {
        return;
    }

    const std::vector<std::string> lines = splitFrameLines(frame);
    if (!terminalInitialized) {
        std::cout << HideTerminalCursor << ClearScreen;
        terminalInitialized = true;
    }

    const int drawableHeight = size.height;
    const bool clipped = static_cast<int>(lines.size()) > drawableHeight;
    const int contentRows = clipped ? std::max(0, drawableHeight - 1) : drawableHeight;

    for (int row = 0; row < contentRows; ++row) {
        std::cout << "\x1b[" << (row + 1) << ";1H" << ClearLine;
        if (row < static_cast<int>(lines.size())) {
            std::cout << clipAnsiLine(lines[row], size.width);
        }
    }

    if (clipped) {
        std::cout << "\x1b[" << drawableHeight << ";1H" << ClearLine
                  << Dim << clipAnsiLine("Output clipped to terminal height - make the window taller to see objectives/events.", size.width)
                  << Reset;
    } else {
        for (int row = contentRows; row < drawableHeight; ++row) {
            std::cout << "\x1b[" << (row + 1) << ";1H" << ClearLine;
        }
    }

    std::cout << "\x1b[1;1H" << std::flush;
    lastFrame = frame;
    lastTerminalWidth = size.width;
    lastTerminalHeight = size.height;
}

void GameTui::renderGame(const Json::Value& root) {
    const Json::Value& gameData = root["gameData"];
    const Json::Value& activePlayer = root["activePlayer"];
    const Json::Value& activeStats = activePlayer["championStats"];
    const Json::Value& allPlayers = root["allPlayers"];
    const double gameTime = asDouble(gameData["gameTime"]);
    const TerminalSize size = terminalSize();
    const bool showItems =
        itemLineMode == 1 ||
        (itemLineMode == 0 && size.height >= (showDetailedStats ? 52 : 42));
    const int objectiveLimit = size.height <= 30 ? 4 : (size.height <= 38 ? 6 : 9);
    const int eventLimit = size.height <= 30 ? 5 : (size.height <= 38 ? 7 : 10);

    const std::vector<Json::Value> orderPlayers = playersForTeam(allPlayers, "ORDER");
    const std::vector<Json::Value> chaosPlayers = playersForTeam(allPlayers, "CHAOS");
    const TeamTotals blueTotals = totalsFor(orderPlayers);
    const TeamTotals redTotals = totalsFor(chaosPlayers);
    const Json::Value activeRosterPlayer = findActiveRosterPlayer(allPlayers, activePlayer);
    const bool hasActiveRosterPlayer = !activeRosterPlayer.isNull();
    const int activeInventory = hasActiveRosterPlayer ? inventoryValue(activeRosterPlayer["items"]) : 0;
    const double activeCurrentGold = asDouble(activePlayer["currentGold"]);

    std::ostringstream out;
    out << Bold << "LOL Live Client TUI" << Reset
              << "  " << Dim << "g GUI, d details, i items, q/Esc quit" << Reset << '\n';

    out << "Game " << Green << formatGameTime(gameTime) << Reset
              << " | " << asString(gameData["gameMode"])
              << " | " << asString(gameData["mapName"]) << " #" << asInt(gameData["mapNumber"])
              << " | Refresh " << refreshMs << "ms\n";

    out << Dim
              << "Source: /liveclientdata/allgamedata only. Gold labels are live estimates: current gold for you, inventory value for everyone."
              << Reset << "\n\n";

    out << Bold << "Active Player" << Reset << '\n';
    if (hasActiveRosterPlayer) {
        out << Gold << asString(activeRosterPlayer["championName"]) << Reset
            << "  " << playerName(activePlayer)
            << "  " << teamColor(asString(activeRosterPlayer["team"], ""))
            << teamDisplayName(asString(activeRosterPlayer["team"], "")) << Reset;
    } else {
        out << Gold << playerName(activePlayer) << Reset;
    }
    out << "  Level " << Green << asInt(activePlayer["level"]) << Reset << '\n';

    out << "Gold " << Gold << formatDouble(activeCurrentGold, 0) << "g current" << Reset;
    if (hasActiveRosterPlayer) {
        out << " | Inventory " << Yellow << activeInventory << "g" << Reset
            << " | Effective gold estimate " << Gold << formatDouble(activeCurrentGold + activeInventory, 0) << "g" << Reset;
    } else {
        out << " | Inventory unavailable until active player is found in allPlayers";
    }
    out << '\n';

    out << "Runes: " << Magenta << runeSummary(activePlayer["fullRunes"]) << Reset << '\n';

    if (showDetailedStats) {
        out << Dim << "Detailed active-player stats enabled." << Reset << '\n';
        out << "Shards " << Magenta << statShardSummary(activePlayer["fullRunes"]) << Reset << '\n';

        out << "HP " << Green << formatDouble(asDouble(activeStats["currentHealth"]), 0)
                  << '/' << formatDouble(asDouble(activeStats["maxHealth"]), 0) << Reset
                  << " | " << asString(activeStats["resourceType"], "Resource") << ' '
                  << Cyan << formatDouble(asDouble(activeStats["resourceValue"]), 0)
                  << '/' << formatDouble(asDouble(activeStats["resourceMax"]), 0) << Reset
                  << " | Regen HP " << Green << formatDouble(asDouble(activeStats["healthRegenRate"]), 1) << Reset
                  << " " << asString(activeStats["resourceType"], "Resource") << ' '
                  << Cyan << formatDouble(asDouble(activeStats["resourceRegenRate"]), 1) << Reset << '\n';

        out << "Offense AD " << Yellow << formatDouble(asDouble(activeStats["attackDamage"]), 0) << Reset
                  << " | AP " << Yellow << formatDouble(asDouble(activeStats["abilityPower"]), 0) << Reset
                  << " | AH " << Yellow << formatDouble(asDouble(activeStats["abilityHaste"]), 0) << Reset
                  << " | AS " << Yellow << formatDouble(asDouble(activeStats["attackSpeed"]), 2) << Reset
                  << " | Range " << Yellow << formatDouble(asDouble(activeStats["attackRange"]), 0) << Reset
                  << " | Crit " << Yellow << formatStatPercent(asDouble(activeStats["critChance"])) << Reset << '\n';

        out << "Defense Armor " << Cyan << formatDouble(asDouble(activeStats["armor"]), 0) << Reset
                  << " | MR " << Cyan << formatDouble(asDouble(activeStats["magicResist"]), 0) << Reset
                  << " | MS " << Cyan << formatDouble(asDouble(activeStats["moveSpeed"]), 0) << Reset
                  << " | Tenacity " << Cyan << formatStatPercent(asDouble(activeStats["tenacity"])) << Reset
                  << " | Lifesteal " << Green << formatStatPercent(asDouble(activeStats["lifeSteal"])) << Reset << '\n';

        out << "Pen Armor flat " << Yellow << formatDouble(asDouble(activeStats["armorPenetrationFlat"]), 0)
                  << " / " << formatPenetrationPercent(asDouble(activeStats["armorPenetrationPercent"])) << Reset
                  << " | Bonus armor pen " << Yellow << formatPenetrationPercent(asDouble(activeStats["bonusArmorPenetrationPercent"])) << Reset
                  << " | Magic flat " << Magenta << formatDouble(asDouble(activeStats["magicPenetrationFlat"]), 0)
                  << " / " << formatPenetrationPercent(asDouble(activeStats["magicPenetrationPercent"])) << Reset
                  << " | Lethality " << Yellow << formatDouble(asDouble(activeStats["physicalLethality"]), 0)
                  << " / Magic lethality " << Magenta << formatDouble(asDouble(activeStats["magicLethality"]), 0) << Reset << '\n';

        out << "Abilities " << Green << abilityLine(activePlayer["abilities"]) << Reset << '\n';
    }

    out << '\n' << Bold << "Scoreboard Summary" << Reset
              << "  Blue inventory " << Cyan << blueTotals.inventory << "g" << Reset
              << " | Red inventory " << Red << redTotals.inventory << "g" << Reset
              << " | Inventory Gold Difference: " << Gold << inventoryDifferenceText(blueTotals.inventory, redTotals.inventory) << Reset
              << '\n';

    if (size.width >= 170) {
        std::ostringstream teamBlock;
        renderTeam(teamBlock, "ORDER", orderPlayers, gameTime, activePlayer, showItems);
        renderTeam(teamBlock, "CHAOS", chaosPlayers, gameTime, activePlayer, showItems);
        renderSidePanel(out, teamBlock.str(), root["events"]["Events"], allPlayers, size.width);
    } else {
        renderTeam(out, "ORDER", orderPlayers, gameTime, activePlayer, showItems);
        renderTeam(out, "CHAOS", chaosPlayers, gameTime, activePlayer, showItems);
        renderObjectives(out, root["events"]["Events"], allPlayers, objectiveLimit);
        renderEvents(out, root["events"]["Events"], eventLimit);
    }

    const std::string itemModeText = itemLineMode == 0 ? (std::string("auto ") + (showItems ? "shown" : "hidden")) :
                                     (itemLineMode == 1 ? "shown" : "hidden");
    out << '\n' << Dim << "Help: d detailed stats " << (showDetailedStats ? "on" : "off")
        << " | i item lines " << itemModeText
        << " | g GUI | q/Esc quit" << Reset << '\n';

    presentFrame(out.str());
}

void GameTui::renderWaiting(const std::string& error) {
    std::ostringstream out;
    out << Bold << "LOL Live Client TUI" << Reset << '\n';
    out << Yellow << "Waiting for an active League of Legends game..." << Reset << "\n\n";
    out << "Polling https://127.0.0.1:2999/liveclientdata/allgamedata every "
              << refreshMs << "ms.\n";
    out << "This local Live Client endpoint needs no lockfile authentication, but only exists while you are in game.\n";
    out << "TLS verification is disabled for 127.0.0.1:2999 because Riot serves a local self-signed certificate.\n\n";
    out << "Last error: " << Red << (error.empty() ? "no response" : error) << Reset << "\n\n";
    out << "Press g for GUI scoreboard, or q/Esc to quit.\n";
    presentFrame(out.str());
}

void GameTui::renderChampSelect(const ChampSelectState& state) {
    std::ostringstream out;
    out << Bold << "LOL Champ Select TUI" << Reset
        << "  " << Dim << "g GUI, q/Esc quits, Ctrl+C also works" << Reset << '\n';
    out << Yellow << state.status << Reset << "\n\n";

    out << Bold << "YOU" << Reset << '\n';
    const std::string lockText = state.myChampionLocked
        ? std::string(Green) + "LOCKED" + Reset
        : std::string(Dim) + "not locked" + Reset;
    out << "Champion " << Gold << (state.myChampionName.empty() ? "not selected" : state.myChampionName) << Reset
        << " | Role " << Cyan << (state.myPosition.empty() ? "ANY" : state.myPosition) << Reset
        << " | " << lockText << '\n';
    out << "Runes " << Magenta << state.runeStatus << Reset << '\n';
    for (const std::string& line : state.runeLines) {
        out << "  " << line << '\n';
    }
    out << "Item Set " << Gold << state.itemSetStatus << Reset << '\n';
    for (const std::string& line : state.itemSetLines) {
        out << "  " << line << '\n';
    }

    out << '\n' << Bold << "Visible Enemy Picks / Counter WR" << Reset << '\n';
    if (state.enemyCounters.empty()) {
        out << Dim << "Waiting for enemy picks to become visible in champ select." << Reset << '\n';
    } else {
        out << std::left << std::setw(6) << "Role"
            << std::setw(16) << "Champion"
            << std::setw(18) << "Matchup"
            << std::setw(10) << "Agg WR"
            << "Games\n";
        for (const EnemyChampionCounter& enemy : state.enemyCounters) {
            out << Dim << std::left << std::setw(6) << (enemy.position.empty() ? "-" : enemy.position) << Reset
                << Red << std::setw(16) << enemy.championName << Reset
                << std::setw(18) << enemy.relation;
            if (enemy.myWinRate >= 0.0) {
                out << Gold << std::setw(10) << (formatDouble(enemy.myWinRate, 1) + "%") << Reset
                    << enemy.games;
            } else {
                out << Dim << std::setw(10) << "NA" << Reset << "NA";
            }
            out << '\n';
        }
    }

    out << "\n" << Dim
        << "Source: League Client lockfile + /lol-champ-select/v1/session. Imports use local /lol-perks and /lol-item-sets."
        << Reset << '\n';
    presentFrame(out.str());
}

bool GameTui::waitForNextFrame() {
    const auto handleAction = [this](const InputAction action) {
        if (action == InputAction::ToggleDetails) {
            showDetailedStats = !showDetailedStats;
            lastFrame.clear();
            return true;
        }
        if (action == InputAction::ToggleItems) {
            itemLineMode = (itemLineMode + 1) % 3;
            lastFrame.clear();
            return true;
        }
        return false;
    };

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(refreshMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const InputAction action = readInputAction();
        if (action == InputAction::Quit) {
            return false;
        }
        if (action == InputAction::Gui) {
            guiRequested = true;
            return false;
        }
        if (handleAction(action)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const InputAction action = readInputAction();
    if (action == InputAction::Gui) {
        guiRequested = true;
    }
    if (handleAction(action)) {
        return true;
    }
    return action == InputAction::None;
}
