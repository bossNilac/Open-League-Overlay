#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "app_paths.h"
#include "app_settings.h"
#include "game_tui.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
constexpr int GuiRequestedExitCode = 2;

struct GuiOverlayProcess {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD processId = 0;
};

struct WindowCloseRequest {
    DWORD processId = 0;
};

BOOL CALLBACK postCloseToProcessWindows(HWND hwnd, LPARAM lParam) {
    auto* request = reinterpret_cast<WindowCloseRequest*>(lParam);
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    if (windowProcessId == request->processId) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

GuiOverlayProcess launchGuiOverlayIfAvailable(const bool manageLifetime = true, const bool scoreboard = false) {
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return {};
    }

    const std::filesystem::path guiPath = std::filesystem::path(modulePath).parent_path() / "LOL_overlay_gui.exe";
    if (!std::filesystem::exists(guiPath)) {
        return {};
    }

    HANDLE job = manageLifetime ? CreateJobObjectW(nullptr, nullptr) : nullptr;
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    std::wstring commandLine = L"\"" + guiPath.wstring() + L"\"";
    if (scoreboard) {
        commandLine += L" --scoreboard";
    }
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(guiPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_PROCESS_GROUP, nullptr, guiPath.parent_path().c_str(),
                       &startupInfo, &processInfo)) {
        CloseHandle(processInfo.hThread);
        if (job) {
            AssignProcessToJobObject(job, processInfo.hProcess);
        }
        if (!manageLifetime) {
            CloseHandle(processInfo.hProcess);
            return {};
        }
        return GuiOverlayProcess{processInfo.hProcess, job, processInfo.dwProcessId};
    }

    if (job) {
        CloseHandle(job);
    }
    return {};
}

void closeGuiOverlay(GuiOverlayProcess& overlay) {
    if (overlay.process) {
        if (overlay.processId != 0) {
            WindowCloseRequest request{overlay.processId};
            EnumWindows(postCloseToProcessWindows, reinterpret_cast<LPARAM>(&request));
        }
        if (WaitForSingleObject(overlay.process, 1500) == WAIT_TIMEOUT) {
            TerminateProcess(overlay.process, 0);
            WaitForSingleObject(overlay.process, 1000);
        }
        CloseHandle(overlay.process);
        overlay.process = nullptr;
        overlay.processId = 0;
    }
    if (overlay.job) {
        CloseHandle(overlay.job);
        overlay.job = nullptr;
    }
}

void savePreferredUiMode(const std::string& mode) {
    AppPaths::ensureDataDirectories();
    AppSettings settings = AppSettingsStore::load();
    settings.preferredUiMode = mode;
    AppSettingsStore::save(settings);
}
}
#endif

int main(int argc, char** argv)
{
    int refreshMs = 1000;
    int consoleFontHeight = 0;
    bool once = false;
    bool startGuiOverlay = true;
    bool autoScaleConsoleFont = true;
    bool forceGui = false;
    bool forceTui = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--once") {
            once = true;
            startGuiOverlay = false;
        } else if (arg == "--refresh-ms" && i + 1 < argc) {
            refreshMs = std::max(250, std::atoi(argv[++i]));
        } else if (arg == "--font-height" && i + 1 < argc) {
            consoleFontHeight = std::clamp(std::atoi(argv[++i]), 6, 32);
            autoScaleConsoleFont = false;
        } else if (arg == "--no-auto-font") {
            autoScaleConsoleFont = false;
        } else if (arg == "--no-gui-overlay") {
            startGuiOverlay = false;
        } else if (arg == "--gui") {
            forceGui = true;
        } else if (arg == "--tui") {
            forceTui = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: LOL_overlay [--gui] [--tui] [--once] [--refresh-ms N] [--font-height N] [--no-auto-font] [--no-gui-overlay]\n";
            std::cout << "Default refresh: 1000ms\n";
            std::cout << "--gui opens the GUI dashboard and remembers GUI mode.\n";
            std::cout << "--tui opens the terminal scoreboard and remembers TUI mode.\n";
            std::cout << "--font-height N forces a smaller/larger Windows console font height, for example 10.\n";
            return 0;
        }
    }

#ifdef _WIN32
    AppPaths::ensureDataDirectories();
    AppPaths::migrateLegacyData(AppPaths::moduleDirectory());
    AppSettings settings = AppSettingsStore::load();
    if (!forceTui && (forceGui || settings.preferredUiMode == "gui")) {
        savePreferredUiMode("gui");
        launchGuiOverlayIfAvailable(false, false);
        launchGuiOverlayIfAvailable(false, true);
        return 0;
    }
    savePreferredUiMode("tui");

    GuiOverlayProcess guiOverlay;
    if (startGuiOverlay) {
        guiOverlay = launchGuiOverlayIfAvailable();
    }
#endif

    GameTui tui(refreshMs, once, consoleFontHeight, autoScaleConsoleFont);
    const int result = tui.run();

#ifdef _WIN32
    if (result == GuiRequestedExitCode) {
        savePreferredUiMode("gui");
        closeGuiOverlay(guiOverlay);
        launchGuiOverlayIfAvailable(false, false);
        launchGuiOverlayIfAvailable(false, true);
        return 0;
    }
    closeGuiOverlay(guiOverlay);
#endif

    return result;
}
