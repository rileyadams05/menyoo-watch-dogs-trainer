#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <ShlObj.h>
#include <commdlg.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <TlHelp32.h>
#include "injector.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using json = nlohmann::json;
using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr wchar_t WINDOW_CLASS[] = L"MenyooWatchDogsTrainerLoader";
static constexpr wchar_t WINDOW_TITLE[] = L"MENYOO Watch Dogs Trainer";
static constexpr wchar_t TARGET_EXE[]   = L"watch_dogs.exe";
static constexpr int     IDI_APP_ICON   = 101;
static constexpr UINT_PTR TIMER_SCAN    = 1;
static constexpr UINT     WM_APP_ERROR    = WM_APP + 1;
static constexpr UINT     WM_APP_INJECTED = WM_APP + 2;

static constexpr int IDC_ACTIVATE_ALL   = 1001;
static constexpr int IDC_DEACTIVATE_ALL = 1002;
static constexpr int IDC_LAUNCH         = 1003;
static constexpr int IDC_INJECT         = 1004;
static constexpr int IDC_OPEN_FOLDER    = 1005;
static constexpr int IDC_EXIT_APP       = 1006;
static constexpr int IDC_CHANGE_PATH    = 1007;

// Layout constants
static constexpr int HEADER_IMAGE_SIZE = 256;
static constexpr int HEADER_PADDING    = 24;
static constexpr int HEADER_BUTTON_SIZE = 36;
static constexpr int HEADER_H  = HEADER_IMAGE_SIZE + HEADER_PADDING * 2; // custom banner height
static constexpr int LEFT_W    = 318;  // left panel width
static constexpr int MARGIN    = 16;
static constexpr int RESIZE_BORDER = 6;

// Table column widths
static constexpr int COL_HK_W  = 124;  // hotkey column
static constexpr int COL_ST_W  = 74;   // state column
static constexpr int ROW_H     = 34;   // cheat row height
static constexpr int CAT_H     = 26;   // category header height
static constexpr int TBL_HDR_H = 82;   // panel header height (title + col headers)

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------
struct CheatOption
{
    std::string  id;
    std::wstring name;
    std::wstring category;
    bool safe    = true;
    bool checked = false;
    UINT vk      = 0;       // assigned hotkey (0 = none)
    RECT bindBtn{};         // [BIND] or key-name click area
    RECT clearBtn{};        // [x] clear button (valid when vk != 0)
    RECT stateBtn{};        // [ON/OFF] toggle
};

struct InjectionComplete
{
    DWORD pid = 0;
    Injector::InjectionResult result;
};

enum class HeaderButton
{
    None,
    Minimize,
    Close
};

// ---------------------------------------------------------------------------
// Window handles
// ---------------------------------------------------------------------------
static HWND g_hWnd             = nullptr;
static HWND g_errorBox         = nullptr;
static HWND g_activateButton   = nullptr;
static HWND g_deactivateButton = nullptr;
static HWND g_openFolderButton = nullptr;
static HWND g_exitButton       = nullptr;

static RECT g_headerCloseBtn{};
static RECT g_headerMinBtn{};
static bool g_headerCloseHot   = false;
static bool g_headerMinHot     = false;
static bool g_headerCloseDown  = false;
static bool g_headerMinDown    = false;
static bool g_headerTracking   = false;
static HeaderButton g_pressedHeaderButton = HeaderButton::None;

// ---------------------------------------------------------------------------
// GDI / GDI+ resources
// ---------------------------------------------------------------------------
static HFONT     g_titleFont    = nullptr;
static HFONT     g_subtitleFont = nullptr;
static HFONT     g_bodyFont     = nullptr;
static HFONT     g_smallFont    = nullptr;
static HFONT     g_tinyFont     = nullptr;
static HFONT     g_logFont      = nullptr;
static HBRUSH    g_bgBrush      = nullptr;
static HBRUSH    g_panelBrush   = nullptr;
static HBRUSH    g_errorBrush   = nullptr;
static ULONG_PTR g_gdiplusToken    = 0;
static Image*    g_backgroundImage = nullptr;
static Image*    g_logoImage       = nullptr;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
enum class LauncherState
{
    WaitingForGame,
    GameDetected,
    WaitingToInject,
    Injecting,
    PostInjectionMonitoring,
    DiagnosticLoaded,
    InjectionFailed,
    GameClosed
};

static LauncherState g_launcherState = LauncherState::WaitingForGame;
static DWORD g_gameDetectedTick = 0;
static bool g_inDisappearGracePeriod = false;
static DWORD g_pidDisappearTick = 0;
static DWORD g_injectionStartTick = 0;
static std::wstring g_lastErrorMessage = L"";

static std::wofstream         g_logFile;
static std::mutex             g_logMutex;
static std::filesystem::path  g_exePath;
static std::filesystem::path  g_exeDir;
static std::filesystem::path  g_dllPath;
static std::filesystem::path  g_launcherConfigPath;
static std::filesystem::path  g_trainerConfigPath;
static std::filesystem::path  g_diagConfigPath;
static std::filesystem::path  g_gameDir;
static std::filesystem::path  g_gameExePath;

static std::vector<CheatOption>  g_cheats;
static std::vector<std::wstring> g_pendingErrors;
static std::atomic_bool g_injecting     = false;
static bool  g_watching        = true;
static bool  g_gameOn          = false;
static bool  g_trainerLoaded   = false;
static DWORD g_processId       = 0;
static DWORD g_lastInjectedPid = 0;
static DWORD g_pidStableTick   = 0;
static int   g_cheatScroll     = 0;
static RECT  g_cheatPanel{};
static int   g_captureRow      = -1;   // row index being bound, -1 = none
static int   g_errorBoxTop     = 400;  // updated by LayoutControls, used for label
static std::unordered_set<DWORD> g_attemptedPids;
static std::mutex                 g_attemptedMutex;
static bool  g_isDiagnosticMode = true;
static std::filesystem::file_time_type g_diagConfigTime{};
static bool  g_hasDiagConfigTime = false;

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
static COLORREF C_BG      = RGB(  6,  8, 10);
static COLORREF C_PANEL   = RGB( 13, 17, 20);
static COLORREF C_PANEL_2 = RGB( 18, 24, 28);
static COLORREF C_BORDER  = RGB( 44, 74, 82);
static COLORREF C_CYAN    = RGB( 32,190,230);
static COLORREF C_WHITE   = RGB(238,246,248);
static COLORREF C_MUTED   = RGB(155,174,180);
static COLORREF C_RED     = RGB(230, 50, 48);
static COLORREF C_GREEN   = RGB( 53,215,103);

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void ErrorLog(const std::wstring& msg);
static void SaveTrainerCheatConfig();
static void ApplyDiagnosticCheatRestrictions();
static void InvalidateMain() { if (g_hWnd) InvalidateRect(g_hWnd, nullptr, FALSE); }

static std::wstring DescribeBinaryArchitecture(const std::filesystem::path& path)
{
    if (path.empty()) return L"(no path)";
    if (!std::filesystem::exists(path)) return L"(missing)";

    DWORD type = 0;
    if (!GetBinaryTypeW(path.c_str(), &type))
    {
        return L"(unknown)";
    }

    switch (type)
    {
    case SCS_32BIT_BINARY: return L"x86";
    case SCS_64BIT_BINARY: return L"x64";
    case SCS_DOS_BINARY:   return L"DOS";
    case SCS_PIF_BINARY:   return L"PIF";
    case SCS_POSIX_BINARY: return L"POSIX";
    case SCS_WOW_BINARY:   return L"WOW";
    default:               return L"(unknown)";
    }
}

static void LoadDiagnosticConfig()
{
    bool previous = g_isDiagnosticMode;
    g_isDiagnosticMode = true; // default to safest mode

    if (!g_diagConfigPath.empty() && std::filesystem::exists(g_diagConfigPath))
    {
        try
        {
            std::ifstream file(g_diagConfigPath);
            if (file.is_open())
            {
                json j;
                file >> j;
                if (j.contains("diagnosticMode") && j["diagnosticMode"].is_boolean())
                    g_isDiagnosticMode = j["diagnosticMode"].get<bool>();
            }
        }
        catch (...)
        {
            LoaderLog::Write(L"Diagnostics: Failed to parse trainer_debug.json. Falling back to diagnosticMode=true.");
            g_isDiagnosticMode = true;
        }
    }
    else
    {
        LoaderLog::Write(L"Diagnostics: trainer_debug.json not found. Using diagnostic mode defaults.");
    }

    LoaderLog::Write(L"Diagnostics: diagnosticMode=" + std::wstring(g_isDiagnosticMode ? L"true" : L"false"));
    ApplyDiagnosticCheatRestrictions();
    if (previous != g_isDiagnosticMode)
        InvalidateMain();
}

static void ReloadDiagnosticsIfModified()
{
    if (g_diagConfigPath.empty()) return;

    try
    {
        if (std::filesystem::exists(g_diagConfigPath))
        {
            auto cur = std::filesystem::last_write_time(g_diagConfigPath);
            if (!g_hasDiagConfigTime || cur != g_diagConfigTime)
            {
                g_diagConfigTime = cur;
                g_hasDiagConfigTime = true;
                LoaderLog::Write(L"Diagnostics: trainer_debug.json changed. Reloading configuration.");
                LoadDiagnosticConfig();
            }
        }
        else if (g_hasDiagConfigTime)
        {
            g_hasDiagConfigTime = false;
            LoaderLog::Write(L"Diagnostics: trainer_debug.json removed. Reverting to diagnostic defaults.");
            LoadDiagnosticConfig();
        }
    }
    catch (...)
    {
    }
}

static bool HasAttemptedPid(DWORD pid)
{
    std::lock_guard<std::mutex> lk(g_attemptedMutex);
    return g_attemptedPids.find(pid) != g_attemptedPids.end();
}

static void RecordAttemptedPid(DWORD pid)
{
    std::lock_guard<std::mutex> lk(g_attemptedMutex);
    g_attemptedPids.insert(pid);
}

static void ClearAttemptedPids()
{
    std::lock_guard<std::mutex> lk(g_attemptedMutex);
    g_attemptedPids.clear();
}

// ---------------------------------------------------------------------------
// VK helpers
// ---------------------------------------------------------------------------
static std::wstring VkToName(UINT vk)
{
    if (!vk) return L"";
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = static_cast<LONG>(sc) << 16;
    switch (vk)
    {
    case VK_INSERT: case VK_DELETE:
    case VK_HOME:   case VK_END:
    case VK_PRIOR:  case VK_NEXT:
    case VK_LEFT:   case VK_RIGHT:
    case VK_UP:     case VK_DOWN:
        lp |= 0x01000000; break;
    default: break;
    }
    wchar_t buf[64]{};
    if (GetKeyNameTextW(lp, buf, static_cast<int>(_countof(buf))) > 0)
        return buf;
    wchar_t fb[16];
    swprintf_s(fb, L"VK_%02X", vk);
    return fb;
}

static bool IsModifierKey(UINT vk)
{
    return vk == VK_SHIFT    || vk == VK_LSHIFT   || vk == VK_RSHIFT   ||
           vk == VK_CONTROL  || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU     || vk == VK_LMENU    || vk == VK_RMENU    ||
           vk == VK_LWIN     || vk == VK_RWIN;
}

// ---------------------------------------------------------------------------
// Timestamp
// ---------------------------------------------------------------------------
static std::wstring GetTimestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
namespace LoaderLog
{
    std::wstring FormatLastError(DWORD err)
    {
        if (!err) return L"No error";
        LPWSTR buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        std::wstring msg = buf ? buf : L"Unknown error";
        if (buf) LocalFree(buf);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L'.'))
            msg.pop_back();
        return msg;
    }

    void Write(const std::wstring& msg)
    {
        const std::wstring line = L"[" + GetTimestamp() + L"] " + msg;
        std::lock_guard<std::mutex> lk(g_logMutex);
        if (g_logFile.is_open()) { g_logFile << line << std::endl; g_logFile.flush(); }
        OutputDebugStringW((line + L"\n").c_str());
    }

    void WriteLastError(const std::wstring& op, DWORD err)
    {
        Write(op + L". Windows error " + std::to_wstring(err) + L": " + FormatLastError(err));
    }
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------
static std::filesystem::path GetExecutablePath()
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        LoaderLog::WriteLastError(L"GetModuleFileNameW failed", GetLastError());
    return path;
}

static std::filesystem::path GetAppDataPath(const wchar_t* fileName)
{
    PWSTR appData = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,
                                        KF_FLAG_CREATE, nullptr, &appData)))
    {
        base = std::filesystem::path(appData) / L"MENYOO Watch Dogs Trainer";
        CoTaskMemFree(appData);
        std::filesystem::create_directories(base);
    }
    else base = g_exeDir;
    return base / fileName;
}

// ---------------------------------------------------------------------------
// Image loading
// ---------------------------------------------------------------------------
static Image* LoadImageFromFile(const std::filesystem::path& p)
{
    if (!std::filesystem::exists(p))
    {
        LoaderLog::Write(L"LoadImageFromFile: File does not exist: " + p.wstring());
        return nullptr;
    }
    Image* img = Image::FromFile(p.c_str(), FALSE);
    if (!img)
    {
        LoaderLog::Write(L"LoadImageFromFile: GDI+ returned null image for " + p.wstring());
        return nullptr;
    }
    Status status = img->GetLastStatus();
    if (status != Ok)
    {
        LoaderLog::Write(L"LoadImageFromFile: Failed to load " + p.wstring() + L". GDI+ Status: " + std::to_wstring(static_cast<int>(status)));
        delete img;
        return nullptr;
    }
    LoaderLog::Write(L"LoadImageFromFile: Successfully loaded " + p.wstring());
    return img;
}

static void LoadLauncherImages()
{
    delete g_backgroundImage;
    delete g_logoImage;
    g_backgroundImage = LoadImageFromFile(g_exeDir / L"background.webp");
    if (!g_backgroundImage)
        g_backgroundImage = LoadImageFromFile(g_exeDir / L"background.png");
    g_logoImage       = nullptr;

    std::filesystem::path icoPath = g_exeDir / L"Icon.ico";
    if (std::filesystem::exists(icoPath))
    {
        HICON ico = static_cast<HICON>(LoadImageW(
            nullptr,
            icoPath.c_str(),
            IMAGE_ICON,
            HEADER_IMAGE_SIZE,
            HEADER_IMAGE_SIZE,
            LR_LOADFROMFILE | LR_CREATEDIBSECTION));
        if (ico)
        {
            g_logoImage = Bitmap::FromHICON(ico);
            DestroyIcon(ico);
            if (!g_logoImage)
                LoaderLog::Write(L"LoadLauncherImages: Bitmap::FromHICON failed for Icon.ico");
        }
        else
        {
            LoaderLog::Write(L"LoadLauncherImages: LoadImageW failed for Icon.ico");
        }
    }

    if (!g_logoImage)
    {
        g_logoImage = LoadImageFromFile(g_exeDir / L"Icon.png");
        if (!g_logoImage)
            g_logoImage = LoadImageFromFile(g_exeDir / L"Icon(3).png");
    }
}

static void DrawCoverImage(Graphics& graphics, Image* image, const Rect& dest)
{
    if (!image || dest.Width <= 0 || dest.Height <= 0) return;

    const REAL imgW = static_cast<REAL>(image->GetWidth());
    const REAL imgH = static_cast<REAL>(image->GetHeight());

    // Dark base fill so letterbox areas aren't transparent
    SolidBrush bgFill(Color(255, 2, 5, 7));
    graphics.FillRectangle(&bgFill, dest);

    // Width-first scale: full image width always visible, centred vertically
    const REAL scale = dest.Width / imgW;
    const REAL drawW = imgW * scale;
    const REAL drawH = imgH * scale;
    const REAL drawX = static_cast<REAL>(dest.X);
    const REAL drawY = dest.Y + (dest.Height - drawH) * 0.5f;

    graphics.DrawImage(image,
        RectF(drawX, drawY, drawW, drawH),
        0.0f, 0.0f, imgW, imgH, UnitPixel);
}

// ---------------------------------------------------------------------------
// Launcher config
// ---------------------------------------------------------------------------
static std::wstring TrimLine(std::wstring v)
{
    while (!v.empty() && (v.back() == L'\r' || v.back() == L'\n' ||
                          v.back() == L' '  || v.back() == L'\t'))
        v.pop_back();
    while (!v.empty() && (v.front() == L' ' || v.front() == L'\t'))
        v.erase(v.begin());
    return v;
}

static void LoadLauncherConfig()
{
    std::wifstream file(g_launcherConfigPath);
    std::wstring line;
    while (std::getline(file, line))
    {
        constexpr wchar_t pfx[] = L"gameDir=";
        if (line.rfind(pfx, 0) == 0)
        {
            g_gameDir     = TrimLine(line.substr(std::size(pfx) - 1));
            g_gameExePath = g_gameDir / TARGET_EXE;
        }
    }
}

static void SaveLauncherConfig()
{
    std::filesystem::create_directories(g_launcherConfigPath.parent_path());
    std::wofstream file(g_launcherConfigPath, std::ios::trunc);
    if (!file.is_open()) { ErrorLog(L"Could not save launcher config"); return; }
    file << L"gameDir=" << g_gameDir.wstring() << std::endl;
    LoaderLog::Write(L"Game path saved: " + g_gameDir.wstring());
}

static bool ValidateGamePath()
{
    if (g_gameDir.empty()) return false;
    g_gameExePath = g_gameDir / TARGET_EXE;
    return std::filesystem::exists(g_gameDir) && std::filesystem::exists(g_gameExePath);
}

static bool PromptForGamePath(HWND owner)
{
    wchar_t fileName[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"watch_dogs.exe\0watch_dogs.exe\0Executable files (*.exe)\0*.exe\0";
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(fileName));
    ofn.lpstrTitle  = L"Select watch_dogs.exe";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;

    std::filesystem::path sel(fileName);
    if (_wcsicmp(sel.filename().c_str(), TARGET_EXE) != 0)
    {
        ErrorLog(L"Invalid selection – please pick watch_dogs.exe.");
        MessageBoxW(owner,
            L"Please select watch_dogs.exe from the Watch Dogs install folder.",
            WINDOW_TITLE, MB_ICONERROR);
        return false;
    }
    g_gameDir     = sel.parent_path();
    g_gameExePath = sel;
    SaveLauncherConfig();
    InvalidateMain();
    return true;
}

// ---------------------------------------------------------------------------
// Cheat list
// ---------------------------------------------------------------------------
static void InitCheats()
{
    g_cheats =
    {
        { "godmode",     L"God Mode",                  L"Player",                    true  },
        { "godmode",     L"Infinite Health",            L"Player",                    true  },
        { "inffocus",    L"Infinite Focus",             L"Player",                    true  },
        { "infbattery",  L"Infinite Battery",           L"Player",                    true  },
        { "infskillpts", L"Infinite Skill Points",      L"Player",                    false },
        { "infmoney",    L"Infinite Money",             L"Player",                    false },
        { "infxp",       L"Infinite XP",                L"Player",                    false },
        { "lockrep",     L"Reputation",                 L"Player",                    false },
        { "notoriety",   L"Notoriety",                  L"Player",                    false },
        { "lockammo",    L"Infinite Ammo",              L"Weapons / Items",           true  },
        { "lockammo",    L"No Reload",                  L"Weapons / Items",           true  },
        { "lockcraft",   L"Infinite Craft Materials",   L"Weapons / Items",           true  },
        { "refillwheel", L"Infinite Items",             L"Weapons / Items",           false },
        { "stealth",     L"Invisible",                  L"Stealth / Police",          false },
        { "stealth",     L"Undetectable",               L"Stealth / Police",          false },
        { "clearheat",   L"Wanted Level Control",       L"Stealth / Police",          true  },
        { "clearheat",   L"Police Radar / Heat Control",L"Stealth / Police",          true  },
        { "noclip",      L"No Clip / Free Roam",        L"World / Movement",          false },
        { "oneteleport", L"Teleport To Waypoint",       L"World / Movement",          false },
        { "savecords",   L"Save Coordinates",           L"World / Movement",          false },
        { "restorecords",L"Restore Coordinates",        L"World / Movement",          false },
        { "settime",     L"Time Of Day",                L"World / Movement",          false },
        { "overidefov",  L"FOV / Camera Distance",      L"World / Movement",          false },
        { "onehitcar",   L"Car Health",                 L"Vehicles",                  false },
        { "onehitcar",   L"One Hit Destroy Vehicles",   L"Vehicles",                  false },
        { "savecords",   L"Vehicle Coordinates",        L"Vehicles",                  false },
        { "infhacktime", L"Infinite Hacking Time",      L"Mini Games / Digital Trips",true  },
        { "spidertimer", L"Spider Tank Cheats",         L"Mini Games / Digital Trips",true  },
        { "cashrunlock", L"Cash Run",                   L"Mini Games / Digital Trips",true  },
        { "nvznlock",    L"NVZN",                       L"Mini Games / Digital Trips",true  },
        { "madnesstimer",L"Madness Timer",              L"Mini Games / Digital Trips",true  },
        { "ctosstop",    L"ctOS Timer",                 L"Mini Games / Digital Trips",true  },
        { "infskillpts", L"Unlock All",                 L"Misc",                      false },
        { "onehitcar",   L"One Hit Kill",               L"Misc",                      false },
        { "poker1",      L"Poker Money",                L"Misc",                      false },
    };
}

static void SyncDuplicateChecks(const std::string& id, bool checked)
{
    if (g_isDiagnosticMode && checked)
        return;
    for (auto& c : g_cheats)
        if (c.id == id) c.checked = checked;
}

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------------------
// Trainer config (JSON) – shared with the injected DLL
// ---------------------------------------------------------------------------
static void LoadTrainerCheatConfig()
{
    std::ifstream file(g_trainerConfigPath);
    if (!file.is_open()) return;
    try
    {
        json j;
        file >> j;

        // Cheat states
        if (j.contains("cheats") && j["cheats"].is_object())
        {
            for (auto& cheat : g_cheats)
            {
                if (j["cheats"].contains(cheat.id))
                    cheat.checked = j["cheats"][cheat.id].get<bool>();
            }
        }

        // Per-name hotkeys saved by the launcher
        if (j.contains("launcherHotkeys") && j["launcherHotkeys"].is_object())
        {
            for (auto& cheat : g_cheats)
            {
                std::string key = WideToUtf8(cheat.name);
                if (j["launcherHotkeys"].contains(key))
                    cheat.vk = j["launcherHotkeys"][key].get<unsigned>();
            }
        }
    }
    catch (...) {}
}

static void ReloadConfigIfModified()
{
    static std::filesystem::file_time_type lastTime;
    static bool hasLastTime = false;

    if (g_trainerConfigPath.empty() || !std::filesystem::exists(g_trainerConfigPath)) return;

    try
    {
        auto curTime = std::filesystem::last_write_time(g_trainerConfigPath);
        if (!hasLastTime || curTime > lastTime)
        {
            lastTime = curTime;
            hasLastTime = true;
            LoadTrainerCheatConfig();
            InvalidateMain();
        }
    }
    catch (...) {}
}

static void SaveTrainerCheatConfig()
{
    json j = json::object();
    {
        std::ifstream in(g_trainerConfigPath);
        try { if (in.is_open()) in >> j; } catch (...) { j = json::object(); }
    }
    if (!j.is_object()) j = json::object();
    if (!j.contains("cheats") || !j["cheats"].is_object()) j["cheats"] = json::object();

    // Cheat on/off states
    for (const auto& c : g_cheats)
        j["cheats"][c.id] = c.checked;

    // Per-name hotkeys (launcher display)
    json lhk = json::object();
    for (const auto& c : g_cheats)
        if (c.vk) lhk[WideToUtf8(c.name)] = c.vk;
    j["launcherHotkeys"] = lhk;

    // Also write to trainer DLL's "hotkeys" section (by ID, first occurrence wins)
    if (!j.contains("hotkeys") || !j["hotkeys"].is_object()) j["hotkeys"] = json::object();
    // Clear old auto-assigned entries before re-writing
    for (const auto& c : g_cheats)
    {
        if (c.vk)
        {
            if (!j["hotkeys"].contains(c.id))
                j["hotkeys"][c.id] = c.vk;
        }
    }

    if (!j.contains("menuHotkey")) j["menuHotkey"] = static_cast<unsigned>(VK_LWIN);
    if (!j.contains("fov"))        j["fov"]        = 75.0f;
    if (!j.contains("timeHours"))  j["timeHours"]  = 12.0f;
    if (!j.contains("maxAmmo"))    j["maxAmmo"]    = 9999;
    if (!j.contains("maxCraft"))   j["maxCraft"]   = 999;

    std::filesystem::create_directories(g_trainerConfigPath.parent_path());
    std::ofstream out(g_trainerConfigPath, std::ios::trunc);
    if (out.is_open()) out << j.dump(2);
}

// ---------------------------------------------------------------------------
// Error log
// ---------------------------------------------------------------------------
static void ErrorLog(const std::wstring& msg)
{
    if (msg.find(L"Access is denied") != std::wstring::npos || msg.find(L"Access Denied") != std::wstring::npos)
    {
        if (g_lastErrorMessage == L"AccessDenied")
        {
            LoaderLog::Write(L"Suppressed duplicate Access Denied error: " + msg);
            return;
        }
        g_lastErrorMessage = L"AccessDenied";
    }
    else
    {
        g_lastErrorMessage = msg;
    }

    LoaderLog::Write(msg);
    if (g_hWnd && IsWindow(g_hWnd))
        PostMessageW(g_hWnd, WM_APP_ERROR, 0,
            reinterpret_cast<LPARAM>(new std::wstring(L"[" + GetTimestamp() + L"] " + msg)));
    else
        g_pendingErrors.push_back(L"[" + GetTimestamp() + L"] " + msg);
}

static void AppendErrorLine(const std::wstring& line)
{
    if (!g_errorBox) return;
    std::wstring text = line + L"\r\n";
    int len = GetWindowTextLengthW(g_errorBox);
    SendMessageW(g_errorBox, EM_SETSEL, len, len);
    SendMessageW(g_errorBox, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g_errorBox, EM_SCROLLCARET, 0, 0);
}

// ---------------------------------------------------------------------------
// Button factory
// ---------------------------------------------------------------------------
static WNDPROC g_oldButtonProc = nullptr;

static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT tme{ sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        if (!GetPropW(hwnd, L"Hovered"))
        {
            SetPropW(hwnd, L"Hovered", reinterpret_cast<HANDLE>(TRUE));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE:
    {
        RemovePropW(hwnd, L"Hovered");
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_DESTROY:
    {
        RemovePropW(hwnd, L"Hovered");
        break;
    }
    }
    return CallWindowProcW(g_oldButtonProc, hwnd, msg, wp, lp);
}

static HWND CreateButton(HWND parent, int id, const wchar_t* text)
{
    HWND h = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_bodyFont), TRUE);
    WNDPROC old = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ButtonSubclassProc)));
    if (old && old != ButtonSubclassProc)
    {
        g_oldButtonProc = old;
    }
    return h;
}



static bool IsProcessRunning(DWORD pid)
{
    if (pid == 0) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe))
    {
        do {
            if (pe.th32ProcessID == pid)
            {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static bool IsProcessAlive(DWORD pid)
{
    if (pid == 0) return false;
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (hProcess)
    {
        DWORD waitResult = WaitForSingleObject(hProcess, 0);
        CloseHandle(hProcess);
        if (waitResult == WAIT_TIMEOUT) return true;
        if (waitResult == WAIT_FAILED) return true; // Assume running if status unknown
        return false;
    }
    return IsProcessRunning(pid);
}

static void SetGameState(DWORD pid)
{
    bool  wasOn  = g_gameOn;
    DWORD oldPid = g_processId;
    g_processId  = pid;
    g_gameOn     = pid != 0;
    if (wasOn != g_gameOn || oldPid != g_processId) InvalidateMain();
}

static bool StartInjection(DWORD pid)
{
    if (g_injecting.exchange(true)) return false;
    g_launcherState = LauncherState::Injecting;
    g_injectionStartTick = GetTickCount();
    SaveTrainerCheatConfig();
    InvalidateMain();
    const std::filesystem::path dllPath = g_dllPath;
    std::thread([pid, dllPath]()
    {
        const std::wstring dllPathStr = dllPath.wstring();
        const bool dllExists = std::filesystem::exists(dllPath);
        const std::wstring startTimestamp = GetTimestamp();
        LoaderLog::Write(L"Auto-injection starting. PID=" + std::to_wstring(pid) +
            L", Process=watch_dogs.exe, DLL=\"" + dllPathStr + L"\", Exists=" + (dllExists ? L"yes" : L"no") +
            L", StartTime=" + startTimestamp);
        auto* complete   = new InjectionComplete{};
        complete->pid    = pid;
        complete->result = Injector::Inject(pid, dllPathStr);

        if (complete->result.success)
        {
            LoaderLog::Write(L"Injection returned success. Starting post-injection monitoring (30 seconds).");
            g_launcherState = LauncherState::PostInjectionMonitoring;
            g_trainerLoaded = false;
            InvalidateMain();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            bool stillRunning = true;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (!IsProcessAlive(pid))
                {
                    stillRunning = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }

            if (!stillRunning)
            {
                LoaderLog::Write(L"watch_dogs.exe exited within 30 seconds after injection. Treating injection as crash/failure.");
                complete->result.success   = false;
                complete->result.stage     = L"PostMonitor";
                complete->result.errorCode = 0;
                complete->result.message   = L"watch_dogs.exe exited within 30 seconds after injection.";
            }
            else
            {
                LoaderLog::Write(L"watch_dogs.exe remained running 30 seconds after injection.");
                LoaderLog::Write(L"DLL loaded and game survived 30 seconds (diagnosticMode=" + std::wstring(g_isDiagnosticMode ? L"true" : L"false") + L")");
            }
        }

        if (g_hWnd && IsWindow(g_hWnd))
            PostMessageW(g_hWnd, WM_APP_INJECTED, 0,
                reinterpret_cast<LPARAM>(complete));
        else delete complete;
    }).detach();
    return true;
}

static void ScanForGame()
{
    static LauncherState lastLauncherState = LauncherState::WaitingForGame;
    static bool lastGameOn = false;
    static DWORD lastProcessId = 0;
    static bool lastTrainerLoaded = false;
    
    // 1. Scan for game process
    DWORD pid = Injector::FindGame();
    
    if (pid == 0)
    {
        // Process is NOT running
        
        // If we were previously tracking a process, initiate or handle grace period
        if (g_processId != 0)
        {
            if (!g_inDisappearGracePeriod)
            {
                // Enter grace period
                g_inDisappearGracePeriod = true;
                g_pidDisappearTick = GetTickCount();
                LoaderLog::Write(L"ScanForGame: Tracked PID " + std::to_wstring(g_processId) + L" disappeared. Starting 5-second grace period.");
            }
            else
            {
                // We are already in grace period, check if it timed out (5 seconds)
                if (GetTickCount() - g_pidDisappearTick >= 5000)
                {
                    LoaderLog::Write(L"ScanForGame: 5-second grace period elapsed with no process found. Game is fully closed. Resetting trainer state.");
                    g_inDisappearGracePeriod = false;
                    
                    g_launcherState = LauncherState::GameClosed;
                    
                    g_processId = 0;
                    g_gameOn = false;
                    g_trainerLoaded = false;
                    g_lastInjectedPid = 0;
                    g_injecting = false;
                    
                    g_launcherState = LauncherState::WaitingForGame;
                }
            }
        }
        else
        {
            // Reset to WaitingForGame if not running
            g_processId = 0;
            g_gameOn = false;
            g_trainerLoaded = false;
            g_launcherState = LauncherState::WaitingForGame;
            g_inDisappearGracePeriod = false;
        }
    }
    else
    {
        // Process IS running
        g_gameOn = true;
        
        // If we were in the grace period, cancel it
        if (g_inDisappearGracePeriod)
        {
            LoaderLog::Write(L"ScanForGame: Process found during grace period. Restoring tracking.");
            g_inDisappearGracePeriod = false;
        }

        const bool alreadyAttempted = HasAttemptedPid(pid);

        if (g_launcherState == LauncherState::WaitingForGame ||
            g_launcherState == LauncherState::GameClosed)
        {
            g_processId = pid;
            g_launcherState = LauncherState::GameDetected;
            g_gameDetectedTick = GetTickCount();
            g_pidStableTick = g_gameDetectedTick;
            if (alreadyAttempted)
            {
                LoaderLog::Write(L"ScanForGame: Watch Dogs detected running (PID: " + std::to_wstring(pid) + L") but auto-injection already attempted for this PID. No automatic retry.");
                if (!g_trainerLoaded)
                    g_launcherState = LauncherState::InjectionFailed;
            }
            else
            {
                g_launcherState = LauncherState::WaitingToInject;
                LoaderLog::Write(L"ScanForGame: Watch Dogs detected running (PID: " + std::to_wstring(pid) + L"). Waiting 15s for stabilization.");
            }
        }
        else
        {
            // Check if PID changed
            if (pid != g_processId)
            {
                LoaderLog::Write(L"ScanForGame: PID changed from " + std::to_wstring(g_processId) + L" to " + std::to_wstring(pid) + L". Updating Process ID.");
                g_processId = pid;
                g_gameDetectedTick = GetTickCount();
                g_pidStableTick = g_gameDetectedTick;
                if (g_launcherState == LauncherState::WaitingToInject)
                {
                    LoaderLog::Write(L"ScanForGame: Restarting wait timer due to PID change.");
                }
                if (!HasAttemptedPid(pid))
                {
                    g_launcherState = LauncherState::WaitingToInject;
                    LoaderLog::Write(L"ScanForGame: Waiting 15s for stabilization on new PID " + std::to_wstring(pid) + L".");
                }
                else if (!g_trainerLoaded)
                {
                    g_launcherState = LauncherState::InjectionFailed;
                }
            }

            if (g_launcherState == LauncherState::WaitingToInject)
            {
                if (!IsProcessAlive(pid))
                {
                    LoaderLog::Write(L"ScanForGame: PID " + std::to_wstring(pid) + L" became unresponsive during stabilization wait. Resetting timer.");
                    g_gameDetectedTick = GetTickCount();
                    g_pidStableTick = g_gameDetectedTick;
                }
                else
                {
                    DWORD elapsed = GetTickCount() - g_pidStableTick;
                    if (elapsed >= 15000 && !HasAttemptedPid(pid))
                    {
                        LoaderLog::Write(L"ScanForGame: 15-second stabilization elapsed. Starting auto-injection.");
                        if (StartInjection(pid))
                            RecordAttemptedPid(pid);
                    }
                }
            }
            else if (g_launcherState == LauncherState::Injecting)
            {
                // Timeout check
                if (GetTickCount() - g_injectionStartTick > 10000)
                {
                    LoaderLog::Write(L"ScanForGame: Injection timeout reached. Setting State -> InjectionFailed.");
                    g_injecting = false;
                    g_launcherState = LauncherState::InjectionFailed;
                    if (!HasAttemptedPid(pid))
                        RecordAttemptedPid(pid);
                    ErrorLog(L"Injection failed. See loader.log for details.");
                }
            }
            else if (g_launcherState == LauncherState::PostInjectionMonitoring)
            {
                // Post-injection monitoring is handled by the worker thread.
                // No watchdog timer here to avoid false failure while waiting.
            }
        }
    }
    
    // Check if any state/status variables changed
    bool changed = (g_launcherState != lastLauncherState) ||
                   (g_gameOn != lastGameOn) ||
                   (g_processId != lastProcessId) ||
                   (g_trainerLoaded != lastTrainerLoaded);
                   
    if (changed)
    {
        lastLauncherState = g_launcherState;
        lastGameOn = g_gameOn;
        lastProcessId = g_processId;
        lastTrainerLoaded = g_trainerLoaded;
        
        InvalidateMain();
    }
}



static void ActivateAll()
{
    if (g_isDiagnosticMode)
    {
        ApplyDiagnosticCheatRestrictions();
        return;
    }
    for (auto& c : g_cheats) c.checked = true;
    SaveTrainerCheatConfig();
    InvalidateMain();
}

static void DeactivateAll()
{
    for (auto& c : g_cheats) c.checked = false;
    SaveTrainerCheatConfig();
    InvalidateMain();
}

static void ApplyDiagnosticCheatRestrictions()
{
    bool diagLocked = g_isDiagnosticMode;
    bool changed    = false;

    if (diagLocked)
    {
        for (auto& cheat : g_cheats)
        {
            if (cheat.checked)
            {
                cheat.checked = false;
                changed = true;
            }
        }
        if (changed)
            SaveTrainerCheatConfig();
        if (g_captureRow >= 0)
            g_captureRow = -1;
    }

    if (g_activateButton && IsWindow(g_activateButton))
    {
        if (diagLocked)
        {
            SetWindowTextW(g_activateButton, L"Cheats disabled in diagnostic mode.");
            EnableWindow(g_activateButton, FALSE);
        }
        else
        {
            SetWindowTextW(g_activateButton, L"Activate All");
            EnableWindow(g_activateButton, TRUE);
        }
    }

    if (g_deactivateButton && IsWindow(g_deactivateButton))
    {
        SetWindowTextW(g_deactivateButton, L"Deactivate All");
        EnableWindow(g_deactivateButton, TRUE);
    }

    if (g_hWnd && IsWindow(g_hWnd))
        InvalidateMain();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static void LayoutControls(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w   = rc.right;
    int h   = rc.bottom;
    int btnH = 30;
    int bx   = MARGIN + 12;
    int bw   = LEFT_W - 24;

    // Error box position on left panel:
    int errTop = HEADER_H + 258;
    int errBoxBottom = h - MARGIN - 86; // leave space for two buttons at the bottom
    int errH = std::max(40, errBoxBottom - errTop);
    g_errorBoxTop = errTop;

    MoveWindow(g_errorBox, bx, errTop, bw, errH, TRUE);

    // Buttons at the bottom of the left panel:
    int btn1Top = h - MARGIN - 72;
    int btn2Top = h - MARGIN - 36;
    MoveWindow(g_openFolderButton, bx, btn1Top, bw, btnH, TRUE);
    MoveWindow(g_exitButton,       bx, btn2Top, bw, btnH, TRUE);

    // Activate / Deactivate in the cheat panel header row (right-aligned)
    int cheatX  = MARGIN + LEFT_W + 12;
    int cheatW  = w - cheatX - MARGIN;
    int abtnW   = 128;
    MoveWindow(g_deactivateButton, cheatX + cheatW - abtnW - 4,           HEADER_H + 8, abtnW, 30, TRUE);
    MoveWindow(g_activateButton,   cheatX + cheatW - (abtnW * 2 + 12),    HEADER_H + 8, abtnW, 30, TRUE);
}

static HeaderButton HitHeaderButton(const POINT& pt)
{
    if (PtInRect(&g_headerCloseBtn, pt)) return HeaderButton::Close;
    if (PtInRect(&g_headerMinBtn, pt))   return HeaderButton::Minimize;
    return HeaderButton::None;
}

static void EnsureHeaderMouseTracking(HWND hwnd)
{
    if (g_headerTracking) return;
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    if (TrackMouseEvent(&tme))
        g_headerTracking = true;
}

static void ResetHeaderHover()
{
    bool changed = g_headerCloseHot || g_headerMinHot ||
                   g_headerCloseDown || g_headerMinDown;
    g_headerCloseHot = g_headerMinHot = false;
    g_headerCloseDown = g_headerMinDown = false;
    g_pressedHeaderButton = HeaderButton::None;
    g_headerTracking = false;
    if (changed)
        InvalidateMain();
}

// ---------------------------------------------------------------------------
// GDI helpers
// ---------------------------------------------------------------------------
static void DrawTextAt(HDC hdc, int x, int y, int w, int h,
                       const std::wstring& text, HFONT font, COLORREF color,
                       UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
    RECT rc{ x, y, x + w, y + h };
    HFONT old = reinterpret_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text.c_str(), -1, &rc, fmt);
    SelectObject(hdc, old);
}

static Color ToGdiColor(COLORREF c, BYTE a = 255)
{
    return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void FillRound(HDC hdc, const RECT& rc, COLORREF color, BYTE alpha = 60, COLORREF borderColor = 0xFFFFFFFF, BYTE borderAlpha = 90)
{
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Rect r(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    GraphicsPath path;
    const int rad = 5;
    path.AddArc(r.X, r.Y, rad, rad, 180, 90);
    path.AddArc(r.X + r.Width - rad, r.Y, rad, rad, 270, 90);
    path.AddArc(r.X + r.Width - rad, r.Y + r.Height - rad, rad, rad, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - rad, rad, rad, 90, 90);
    path.CloseFigure();
    SolidBrush brush(ToGdiColor(color, alpha));
    COLORREF bc = (borderColor == 0xFFFFFFFF) ? C_BORDER : borderColor;
    Pen        pen(ToGdiColor(bc, borderAlpha), 1.0f);
    g.FillPath(&brush, &path);
    g.DrawPath(&pen,  &path);
}

static void PaintHeaderButton(HDC hdc, const RECT& rc, bool hot, bool down, const wchar_t* glyph)
{
    Graphics g(hdc);
    Color fill = down ? Color(220, 32, 44, 52)
                      : (hot ? Color(180, 24, 32, 38)
                             : Color(150, 18, 24, 28));
    Color border = down ? Color(220, 64, 94, 112)
                        : Color(150, 44, 74, 82);
    SolidBrush brush(fill);
    Pen pen(border, 1.0f);
    Rect rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    g.FillRectangle(&brush, rect);
    g.DrawRectangle(&pen, Rect(rect.X, rect.Y, rect.Width - 1, rect.Height - 1));
    DrawTextAt(hdc, rc.left, rc.top, rect.Width, rect.Height, glyph,
               g_smallFont, C_WHITE,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Cheat table helpers
// ---------------------------------------------------------------------------
static int GetCheatContentHeight()
{
    int total = 0;
    std::wstring last;
    for (const auto& c : g_cheats)
    {
        if (c.category != last) { total += CAT_H; last = c.category; }
        total += ROW_H;
    }
    return total;
}

static int GetMaxCheatScroll()
{
    if (g_cheatPanel.bottom <= g_cheatPanel.top) return 0;
    int viewH = std::max(0,
        static_cast<int>((g_cheatPanel.bottom - 4) - (g_cheatPanel.top + TBL_HDR_H)));
    return std::max(0, GetCheatContentHeight() - viewH);
}

static void ClampCheatScroll()
{
    g_cheatScroll = std::clamp(g_cheatScroll, 0, GetMaxCheatScroll());
}

// ---------------------------------------------------------------------------
// Cheat table paint
// ---------------------------------------------------------------------------
static void PaintCheats(HDC hdc, const RECT& panel)
{
    // Panel background
    FillRound(hdc, panel, C_PANEL);

    // ── Panel header ──────────────────────────────────────────────────────────
    DrawTextAt(hdc, panel.left + 14, panel.top + 10, 200, 30,
               L"CHEATS", g_subtitleFont, C_WHITE);

    if (g_isDiagnosticMode)
    {
        DrawTextAt(hdc, panel.left + 14, panel.top + 42,
                   (panel.right - panel.left) - 28, 24,
                   L"Cheats disabled in diagnostic mode.",
                   g_tinyFont, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // Separator under panel title
    {
        Graphics sg(hdc);
        Pen sep(ToGdiColor(C_BORDER, 80), 1.0f);
        int sy = panel.top + TBL_HDR_H - 30;
        sg.DrawLine(&sep, panel.left + 8, sy, panel.right - 8, sy);
    }

    // ── Column headers ────────────────────────────────────────────────────────
    int cx   = panel.left + 10;
    int cw   = (panel.right - panel.left) - 20;
    int nmW  = cw - COL_HK_W - COL_ST_W - 8;
    int cy   = panel.top + TBL_HDR_H - 26;

    DrawTextAt(hdc, cx,                              cy, COL_HK_W, 20,
               L"HOTKEY", g_tinyFont, C_CYAN);
    DrawTextAt(hdc, cx + COL_HK_W + 4,              cy, nmW,      20,
               L"CHEAT NAME", g_tinyFont, C_CYAN);
    DrawTextAt(hdc, cx + COL_HK_W + 4 + nmW,        cy, COL_ST_W, 20,
               L"STATE", g_tinyFont, C_CYAN,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Thin separator under column headers
    {
        Graphics sg(hdc);
        Pen sep(ToGdiColor(C_BORDER, 55), 1.0f);
        int sy = panel.top + TBL_HDR_H - 4;
        sg.DrawLine(&sep, panel.left + 8, sy, panel.right - 8, sy);
    }

    // ── Scrollable table body ─────────────────────────────────────────────────
    RECT clip{ panel.left + 4, panel.top + TBL_HDR_H,
               panel.right - 4, panel.bottom - 4 };
    ClampCheatScroll();

    HRGN oldRgn  = CreateRectRgn(0, 0, 0, 0);
    int  hasOld  = GetClipRgn(hdc, oldRgn);
    HRGN clipRgn = CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom);
    SelectClipRgn(hdc, clipRgn);

    int y = clip.top - g_cheatScroll;
    std::wstring lastCat;

    for (int i = 0; i < static_cast<int>(g_cheats.size()); ++i)
    {
        auto& cheat = g_cheats[i];

        // Category header row
        if (cheat.category != lastCat)
        {
            if (y + CAT_H > clip.top && y < clip.bottom)
            {
                // Subtle row tint
                Graphics cg(hdc);
                SolidBrush cb(ToGdiColor(C_PANEL_2, 30));
                cg.FillRectangle(&cb,
                    static_cast<INT>(clip.left), static_cast<INT>(y),
                    static_cast<INT>(clip.right - clip.left), static_cast<INT>(CAT_H));
                // Cyan left accent bar
                SolidBrush accent(ToGdiColor(C_CYAN, 160));
                cg.FillRectangle(&accent, clip.left, y + 4, 3, CAT_H - 8);
                // Category name
                DrawTextAt(hdc, clip.left + 10, y,
                           (clip.right - clip.left) - 10, CAT_H,
                           cheat.category, g_tinyFont, C_CYAN,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            y += CAT_H;
            lastCat = cheat.category;
        }

        bool vis = (y + ROW_H > clip.top && y < clip.bottom);

        // Row background
        if (vis)
        {
            bool cap = (g_captureRow == i);
            COLORREF bg = cap
                ? RGB(20, 52, 38)
                : (cheat.checked ? RGB(10, 28, 18) : RGB(8, 11, 14));
            BYTE ba = cap ? 130 : 38;
            Graphics rg(hdc);
            SolidBrush rb(ToGdiColor(bg, ba));
            rg.FillRectangle(&rb, clip.left, y,
                              clip.right - clip.left, ROW_H - 1);
            Pen rp(ToGdiColor(C_BORDER, 22), 1.0f);
            rg.DrawLine(&rp,
                static_cast<INT>(clip.left + 4), static_cast<INT>(y + ROW_H - 1),
                static_cast<INT>(clip.right - 4), static_cast<INT>(y + ROW_H - 1));
        }

        // Column anchors
        int hkX = clip.left + 4;
        int nmX = hkX + COL_HK_W + 4;
        int nmCW = (clip.right - 4) - nmX - COL_ST_W - 4;
        int stX  = (clip.right - 4) - COL_ST_W;

        if (vis)
        {
            // ── Hotkey column ─────────────────────────────────────────────────
            if (cheat.vk == 0)
            {
                // Unbound: show [+ BIND] button
                RECT bindR{ hkX + 2, y + 5, hkX + 82, y + ROW_H - 5 };
                cheat.bindBtn  = bindR;
                cheat.clearBtn = { 0, 0, 0, 0 };
                FillRound(hdc, bindR, RGB(16, 22, 26), 255, RGB(22, 90, 110), 255);
                DrawTextAt(hdc, bindR.left, bindR.top,
                           bindR.right - bindR.left, bindR.bottom - bindR.top,
                           L"+ BIND", g_tinyFont, C_WHITE,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                // Bound: show [KEY NAME] + [x] clear
                std::wstring kn = VkToName(cheat.vk);
                RECT keyR{ hkX + 2,  y + 5,  hkX + 80, y + ROW_H - 5 };
                RECT clrR{ hkX + 84, y + 7,  hkX + 108, y + ROW_H - 7 };
                cheat.bindBtn  = keyR;
                cheat.clearBtn = clrR;
                FillRound(hdc, keyR, RGB(12, 38, 58), 255, C_CYAN, 255);
                DrawTextAt(hdc, keyR.left, keyR.top,
                           keyR.right - keyR.left, keyR.bottom - keyR.top,
                           kn, g_tinyFont, C_CYAN,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                FillRound(hdc, clrR, RGB(44, 14, 14), 255, RGB(180, 44, 44), 255);
                DrawTextAt(hdc, clrR.left, clrR.top,
                           clrR.right - clrR.left, clrR.bottom - clrR.top,
                           L"x", g_tinyFont, C_RED,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // ── Cheat name ────────────────────────────────────────────────────
            bool cap = (g_captureRow == i);
            DrawTextAt(hdc, nmX, y, nmCW, ROW_H,
                       cheat.name, g_bodyFont, cap ? C_CYAN : C_WHITE,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // ── State button ──────────────────────────────────────────────────
            RECT stR{ stX + 2, y + 5, stX + COL_ST_W - 2, y + ROW_H - 5 };
            cheat.stateBtn = stR;
            
            bool diagLocked = g_isDiagnosticMode;
            std::wstring stateText = diagLocked ? L"DISABLED" : L"OFF";
            COLORREF stateColor = diagLocked ? C_MUTED : C_RED;
            COLORREF stateBgColor = diagLocked ? RGB(28, 32, 34) : RGB(44, 12, 14);

            if (!diagLocked && cheat.checked)
            {
                if (g_launcherState == LauncherState::DiagnosticLoaded)
                {
                    stateText = L"ON";
                    stateColor = C_GREEN;
                    stateBgColor = RGB(10, 44, 22);
                }
                else if (g_launcherState == LauncherState::PostInjectionMonitoring)
                {
                    stateText = L"VERIFYING";
                    stateColor = C_CYAN;
                    stateBgColor = RGB(10, 44, 52);
                }
                else if (g_launcherState == LauncherState::Injecting)
                {
                    stateText = L"QUEUED";
                    stateColor = C_CYAN;
                    stateBgColor = RGB(10, 44, 52);
                }
                else
                {
                    stateText = L"QUEUED";
                    stateColor = RGB(240, 173, 78);
                    stateBgColor = RGB(52, 38, 10);
                }
            }
            FillRound(hdc, stR, stateBgColor, 255, stateColor, 255);
            HFONT sFont = (stateText == L"QUEUED") ? g_tinyFont : g_smallFont;
            DrawTextAt(hdc, stR.left, stR.top,
                       stR.right - stR.left, stR.bottom - stR.top,
                       stateText, sFont,
                       stateColor,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else
        {
            cheat.bindBtn  = { 0, 0, 0, 0 };
            cheat.clearBtn = { 0, 0, 0, 0 };
            cheat.stateBtn = { 0, 0, 0, 0 };
        }

        y += ROW_H;
    }

    // Restore clip region
    if (hasOld == 1) SelectClipRgn(hdc, oldRgn);
    else             SelectClipRgn(hdc, nullptr);
    DeleteObject(oldRgn);
    DeleteObject(clipRgn);

    // ── Capture-mode banner (drawn over the table top) ────────────────────────
    if (g_captureRow >= 0 && g_captureRow < static_cast<int>(g_cheats.size()))
    {
        RECT banner{ panel.left + 4, clip.top, panel.right - 4, clip.top + 36 };
        Graphics bg(hdc);
        SolidBrush bb(Color(215, 4, 40, 30));
        bg.FillRectangle(&bb,
            static_cast<INT>(banner.left), static_cast<INT>(banner.top),
            static_cast<INT>(banner.right  - banner.left),
            static_cast<INT>(banner.bottom - banner.top));
        std::wstring msg =
            L"  Press a key to bind:  "
            + g_cheats[g_captureRow].name
            + L"     (ESC = cancel  |  Del = clear)";
        DrawTextAt(hdc, banner.left + 8, banner.top,
                   banner.right - banner.left - 16, 36,
                   msg, g_smallFont, C_CYAN,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------------------
// Main paint
// ---------------------------------------------------------------------------
static void PaintMain(HDC hdc, HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    Graphics graphics(hdc);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    // Background image
    Rect windowRect(0, 0, w, h);
    if (g_backgroundImage)
        DrawCoverImage(graphics, g_backgroundImage, windowRect);
    else
    {
        SolidBrush fb(ToGdiColor(C_BG));
        graphics.FillRectangle(&fb, windowRect);
    }

    // Dark transparent overlay so the UI text stays readable
    SolidBrush overlay(Color(150, 2, 5, 7));
    graphics.FillRectangle(&overlay, windowRect);

    // Custom header banner ----------------------------------------------------
    Rect headerRect(0, 0, w, HEADER_H);
    SolidBrush headerBg(Color(255, 12, 16, 20));
    graphics.FillRectangle(&headerBg, headerRect);
    Pen headerLine(Color(200, 44, 74, 82), 1.0f);
    graphics.DrawLine(&headerLine, 0, HEADER_H - 1, w, HEADER_H - 1);

    int artSize = std::min(HEADER_IMAGE_SIZE, HEADER_H - HEADER_PADDING * 2);
    int artX    = MARGIN;
    int artY    = HEADER_PADDING;
    if (g_logoImage)
    {
        graphics.DrawImage(g_logoImage, Rect(artX, artY, artSize, artSize),
            0, 0,
            static_cast<REAL>(g_logoImage->GetWidth()),
            static_cast<REAL>(g_logoImage->GetHeight()),
            UnitPixel);
    }

    int textLeft  = artX + artSize + HEADER_PADDING;
    int textWidth = std::max(200, w - textLeft - MARGIN - (HEADER_BUTTON_SIZE * 2) - 48);
    int titleY    = artY + 8;
    int subtitleY = titleY + 64;

    DrawTextAt(hdc, textLeft, titleY,  textWidth, 60,
               L"MENYOO WATCH DOGS TRAINER", g_titleFont, C_WHITE);
    DrawTextAt(hdc, textLeft, subtitleY, textWidth, 36,
               L"Watch Dogs 2014 Steam Trainer", g_subtitleFont, C_CYAN);

    // Header buttons (minimise / close)
    int btnY = artY;
    g_headerCloseBtn = { w - MARGIN - HEADER_BUTTON_SIZE,
                         btnY,
                         w - MARGIN,
                         btnY + HEADER_BUTTON_SIZE };
    g_headerMinBtn   = { g_headerCloseBtn.left - HEADER_BUTTON_SIZE - 12,
                         btnY,
                         g_headerCloseBtn.left - 12,
                         btnY + HEADER_BUTTON_SIZE };

    PaintHeaderButton(hdc, g_headerMinBtn,   g_headerMinHot,   g_headerMinDown,   L"–");
    PaintHeaderButton(hdc, g_headerCloseBtn, g_headerCloseHot, g_headerCloseDown, L"×");

    // Trainer status text next to buttons
    int statusRight = g_headerMinBtn.left - 16;
    int statusWidth = 260;
    int statusX     = std::max(textLeft, statusRight - statusWidth);
    int statusY     = titleY + 4;

    std::wstring st = L"Trainer not loaded";
    COLORREF sc = C_MUTED;
    if (g_launcherState == LauncherState::WaitingToInject ||
        g_launcherState == LauncherState::GameDetected)
    {
        st = L"Waiting to inject";
        sc = C_CYAN;
    }
    else if (g_launcherState == LauncherState::Injecting)
    {
        st = L"Injecting trainer...";
        sc = C_CYAN;
    }
    else if (g_launcherState == LauncherState::PostInjectionMonitoring)
    {
        st = L"Verifying diagnostic load";
        sc = C_CYAN;
    }
    else if (g_launcherState == LauncherState::DiagnosticLoaded)
    {
        st = g_isDiagnosticMode ? L"Diagnostic loaded" : L"Trainer loaded";
        sc = C_GREEN;
    }
    else if (g_launcherState == LauncherState::InjectionFailed)
    {
        st = L"Trainer failed";
        sc = C_RED;
    }

    DrawTextAt(hdc, statusX, statusY, statusWidth, 34, st, g_bodyFont, sc,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // ── Left panel ────────────────────────────────────────────────────────────
    RECT left{ MARGIN, HEADER_H, MARGIN + LEFT_W, h - MARGIN };
    FillRound(hdc, left, C_PANEL);

    DrawTextAt(hdc, left.left + 14, left.top + 10, LEFT_W - 28, 26,
               L"Watch Dogs", g_subtitleFont, C_WHITE);

    // Separator under panel title
    {
        Graphics sg(hdc);
        Pen sep(ToGdiColor(C_BORDER, 70), 1.0f);
        sg.DrawLine(&sep,
            static_cast<INT>(left.left + 8),  static_cast<INT>(left.top + 42),
            static_cast<INT>(left.right - 8), static_cast<INT>(left.top + 42));
    }

    // Info rows
    int iy = left.top + 52;
    int lx = left.left + 14;
    int vx = lx + 118;
    int vw = LEFT_W - 118 - 28;

    auto infoRow = [&](const wchar_t* label, const std::wstring& val, COLORREF vc)
    {
        DrawTextAt(hdc, lx, iy, 114, 24, label, g_bodyFont, C_MUTED);
        DrawTextAt(hdc, vx, iy, vw,  24, val,   g_bodyFont, vc);
        iy += 28;
    };

    infoRow(L"Process Name:", TARGET_EXE, C_WHITE);
    infoRow(L"Game Status:",
            g_gameOn ? L"Game Is ON" : L"Game Is OFF",
            g_gameOn ? C_GREEN : C_RED);
    infoRow(L"Process ID:", std::to_wstring(g_processId), C_WHITE);

    std::wstring tStatus = L"Not loaded";
    COLORREF tColor = C_MUTED;
    if (g_launcherState == LauncherState::GameDetected)
    {
        tStatus = L"Detected";
        tColor = C_CYAN;
    }
    else if (g_launcherState == LauncherState::WaitingToInject)
    {
        tStatus = L"Waiting";
        tColor = C_CYAN;
    }
    else if (g_launcherState == LauncherState::Injecting)
    {
        tStatus = L"Injecting";
        tColor = C_CYAN;
    }
    else if (g_launcherState == LauncherState::PostInjectionMonitoring)
    {
        tStatus = L"Verifying";
        tColor = C_CYAN;
    }
    else if (g_launcherState == LauncherState::DiagnosticLoaded)
    {
        tStatus = g_isDiagnosticMode ? L"Diagnostic Loaded" : L"Loaded";
        tColor = g_isDiagnosticMode ? C_CYAN : C_GREEN;
    }
    else if (g_launcherState == LauncherState::InjectionFailed)
    {
        tStatus = L"Failed";
        tColor = C_RED;
    }
    std::wstring trainerRowText = tStatus;
    COLORREF trainerRowColor = tColor;
    if (g_launcherState == LauncherState::PostInjectionMonitoring)
    {
        trainerRowText = L"Verifying";
        trainerRowColor = C_CYAN;
    }
    else if (g_launcherState == LauncherState::DiagnosticLoaded)
    {
        trainerRowText = g_isDiagnosticMode ? L"Diagnostic Loaded" : L"Trainer Loaded";
        trainerRowColor = g_isDiagnosticMode ? C_CYAN : C_GREEN;
    }
    infoRow(L"Trainer:", trainerRowText, trainerRowColor);
    infoRow(L"Diagnostic Mode:", g_isDiagnosticMode ? L"On" : L"Off",
            g_isDiagnosticMode ? C_CYAN : C_MUTED);

    // If game is not running, show instructions
    if (!g_gameOn)
    {
        RECT msgRect{ left.left + 14, left.top + 172, left.right - 14, left.top + 242 };
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, g_smallFont));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(240, 173, 78)); // orange/yellow
        DrawTextW(hdc, L"Start Watch Dogs first, load into the game, then open this trainer.", -1, &msgRect, DT_LEFT | DT_WORDBREAK);
        SelectObject(hdc, oldFont);
    }

    // Error log label (positioned relative to the error box WIN32 control)
    if (g_errorBoxTop > 0)
        DrawTextAt(hdc, lx, g_errorBoxTop - 20, 100, 18,
                   L"Error Log", g_tinyFont, C_MUTED);

    // ── Right panel (cheat table) ─────────────────────────────────────────────
    g_cheatPanel =
    {
        left.right + 12, HEADER_H,
        rc.right - MARGIN, rc.bottom - MARGIN
    };
    PaintCheats(hdc, g_cheatPanel);
}

// ---------------------------------------------------------------------------
// Owner-draw button
// ---------------------------------------------------------------------------
static void DrawButton(const DRAWITEMSTRUCT* dis)
{
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool hot      = !disabled && (GetPropW(dis->hwndItem, L"Hovered") != nullptr);

    COLORREF fill = disabled ? RGB(30, 32, 34)
                  : (pressed ? RGB(10, 22, 32)
                  : (hot     ? RGB(22, 48, 68)
                             : RGB(16, 22, 26)));

    COLORREF border = disabled ? RGB(55, 58, 60)
                    : ((pressed || hot) ? RGB(32, 190, 230)
                                        : RGB(22, 90, 110));

    COLORREF textColor = disabled ? RGB(110, 115, 120) : RGB(238, 246, 248);

    Graphics g(dis->hDC);
    SolidBrush brush(ToGdiColor(fill, 255));
    Pen        pen(ToGdiColor(border, 255), 1.0f);

    g.FillRectangle(&brush,
        static_cast<INT>(dis->rcItem.left),
        static_cast<INT>(dis->rcItem.top),
        static_cast<INT>(dis->rcItem.right  - dis->rcItem.left),
        static_cast<INT>(dis->rcItem.bottom - dis->rcItem.top));

    g.DrawRectangle(&pen,
        static_cast<INT>(dis->rcItem.left),
        static_cast<INT>(dis->rcItem.top),
        static_cast<INT>(dis->rcItem.right  - dis->rcItem.left - 1),
        static_cast<INT>(dis->rcItem.bottom - dis->rcItem.top - 1));

    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);
    DrawTextAt(dis->hDC,
        dis->rcItem.left, dis->rcItem.top,
        dis->rcItem.right  - dis->rcItem.left,
        dis->rcItem.bottom - dis->rcItem.top,
        text, g_smallFont, textColor,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    // ── Creation ──────────────────────────────────────────────────────────────
    case WM_CREATE:
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        g_activateButton   = CreateButton(hwnd, IDC_ACTIVATE_ALL,   L"Activate All");
        g_deactivateButton = CreateButton(hwnd, IDC_DEACTIVATE_ALL, L"Deactivate All");
        g_openFolderButton = CreateButton(hwnd, IDC_OPEN_FOLDER,    L"Open Release Folder");
        g_exitButton       = CreateButton(hwnd, IDC_EXIT_APP,       L"Exit");

        g_errorBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(g_errorBox, WM_SETFONT,
            reinterpret_cast<WPARAM>(g_logFont), TRUE);

        LayoutControls(hwnd);
        ApplyDiagnosticCheatRestrictions();
        for (const auto& e : g_pendingErrors) AppendErrorLine(e);
        g_pendingErrors.clear();
        SetTimer(hwnd, TIMER_SCAN, 2000, nullptr);
        return 0;
    }

    // ── Resize ────────────────────────────────────────────────────────────────
    case WM_SIZE:
        LayoutControls(hwnd);
        ClampCheatScroll();
        ApplyDiagnosticCheatRestrictions();
        InvalidateMain();
        return 0;

    // ── Min size ──────────────────────────────────────────────────────────────
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = 960;
        info->ptMinTrackSize.y = 820;
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);

        RECT rc{};
        GetClientRect(hwnd, &rc);

        bool inClose = PtInRect(&g_headerCloseBtn, pt) != 0;
        bool inMin   = PtInRect(&g_headerMinBtn, pt) != 0;
        if (inClose || inMin)
            return HTCLIENT;

        bool left   = pt.x < RESIZE_BORDER;
        bool right  = pt.x >= rc.right - RESIZE_BORDER;
        bool top    = pt.y < RESIZE_BORDER;
        bool bottom = pt.y >= rc.bottom - RESIZE_BORDER;

        if (top && left)   return HTTOPLEFT;
        if (top && right)  return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (top)    return HTTOP;
        if (bottom) return HTBOTTOM;
        if (left)   return HTLEFT;
        if (right)  return HTRIGHT;
        if (pt.y < HEADER_H)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        HeaderButton hit = HitHeaderButton(pt);
        bool newCloseHot = (hit == HeaderButton::Close);
        bool newMinHot   = (hit == HeaderButton::Minimize);

        bool newCloseDown = (g_pressedHeaderButton == HeaderButton::Close && newCloseHot);
        bool newMinDown   = (g_pressedHeaderButton == HeaderButton::Minimize && newMinHot);

        if (newCloseHot || newMinHot || g_pressedHeaderButton != HeaderButton::None)
            EnsureHeaderMouseTracking(hwnd);

        bool changed = (newCloseHot != g_headerCloseHot) ||
                       (newMinHot   != g_headerMinHot)   ||
                       (newCloseDown != g_headerCloseDown) ||
                       (newMinDown   != g_headerMinDown);

        g_headerCloseHot  = newCloseHot;
        g_headerMinHot    = newMinHot;
        g_headerCloseDown = newCloseDown;
        g_headerMinDown   = newMinDown;

        if (changed)
            InvalidateMain();
        break;
    }

    case WM_MOUSELEAVE:
        ResetHeaderHover();
        break;

    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != hwnd)
            ResetHeaderHover();
        break;

    // ── Keyboard – hotkey capture + cheat toggle ───────────────────────────────
    case WM_KEYDOWN:
    {
        UINT vk = static_cast<UINT>(wp);

        if (g_captureRow >= 0)
        {
            // Capture mode: assign, cancel, or clear
            if (vk == VK_ESCAPE)
            {
                g_captureRow = -1;
            }
            else if (vk == VK_BACK || vk == VK_DELETE)
            {
                if (g_captureRow < static_cast<int>(g_cheats.size()))
                    g_cheats[g_captureRow].vk = 0;
                g_captureRow = -1;
                SaveTrainerCheatConfig();
            }
            else if (!IsModifierKey(vk))
            {
                if (g_captureRow < static_cast<int>(g_cheats.size()))
                    g_cheats[g_captureRow].vk = vk;
                g_captureRow = -1;
                SaveTrainerCheatConfig();
            }
            InvalidateMain();
            return 0;
        }

        // Normal operation: toggle cheat if game + trainer are active
        if (!g_isDiagnosticMode && g_gameOn && g_trainerLoaded)
        {
            for (auto& cheat : g_cheats)
            {
                if (cheat.vk && cheat.vk == vk)
                {
                    SyncDuplicateChecks(cheat.id, !cheat.checked);
                    SaveTrainerCheatConfig();
                    InvalidateMain();
                    return 0;
                }
            }
        }
        break;
    }

    // ── Scan timer ────────────────────────────────────────────────────────────
    case WM_TIMER:
        if (wp == TIMER_SCAN)
        {
            ReloadConfigIfModified();
            ReloadDiagnosticsIfModified();
            ScanForGame();
        }
        return 0;

    // ── Mouse wheel – scroll cheat table ─────────────────────────────────────
    case WM_MOUSEWHEEL:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g_cheatPanel, pt))
        {
            g_cheatScroll =
                std::max(0, g_cheatScroll - GET_WHEEL_DELTA_WPARAM(wp) / 3);
            ClampCheatScroll();
            InvalidateMain();
            return 0;
        }
        break;
    }

    // ── Left click – table row hit test ──────────────────────────────────────
    case WM_LBUTTONDOWN:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        HeaderButton hit = HitHeaderButton(pt);
        if (hit != HeaderButton::None)
        {
            g_pressedHeaderButton = hit;
            g_headerCloseDown = (hit == HeaderButton::Close);
            g_headerMinDown   = (hit == HeaderButton::Minimize);
            EnsureHeaderMouseTracking(hwnd);
            SetCapture(hwnd);
            InvalidateMain();
            return 0;
        }
        bool hitSomething = false;

        for (int i = 0; i < static_cast<int>(g_cheats.size()); ++i)
        {
            auto& cheat = g_cheats[i];

            // State toggle
            if (cheat.stateBtn.right > cheat.stateBtn.left &&
                PtInRect(&cheat.stateBtn, pt))
            {
                if (!g_isDiagnosticMode)
                {
                    SyncDuplicateChecks(cheat.id, !cheat.checked);
                    SaveTrainerCheatConfig();
                    InvalidateMain();
                }
                hitSomething = true;
                break;
            }
            // Clear hotkey
            if (cheat.clearBtn.right > cheat.clearBtn.left &&
                PtInRect(&cheat.clearBtn, pt))
            {
                cheat.vk = 0;
                SaveTrainerCheatConfig();
                InvalidateMain();
                hitSomething = true;
                break;
            }
            // Bind / re-bind hotkey (toggle capture)
            if (cheat.bindBtn.right > cheat.bindBtn.left &&
                PtInRect(&cheat.bindBtn, pt))
            {
                g_captureRow = (g_captureRow == i) ? -1 : i;
                if (g_captureRow >= 0) SetFocus(hwnd);
                InvalidateMain();
                hitSomething = true;
                break;
            }
        }

        // Click outside interactive elements cancels capture
        if (!hitSomething && g_captureRow >= 0)
        {
            g_captureRow = -1;
            InvalidateMain();
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (g_pressedHeaderButton != HeaderButton::None)
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            HeaderButton hit = HitHeaderButton(pt);
            HeaderButton pressed = g_pressedHeaderButton;
            ReleaseCapture();
            ResetHeaderHover();
            if (pressed == HeaderButton::Close && hit == HeaderButton::Close)
            {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            else if (pressed == HeaderButton::Minimize && hit == HeaderButton::Minimize)
            {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
            return 0;
        }
        break;
    }

    // ── Button commands ───────────────────────────────────────────────────────
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_ACTIVATE_ALL:
            ActivateAll();
            return 0;
        case IDC_DEACTIVATE_ALL:
            DeactivateAll();
            return 0;
        case IDC_OPEN_FOLDER:
            ShellExecuteW(hwnd, L"open",
                g_exeDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case IDC_EXIT_APP:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    // ── Async messages ────────────────────────────────────────────────────────
    case WM_APP_ERROR:
    {
        auto* line = reinterpret_cast<std::wstring*>(lp);
        if (line) { AppendErrorLine(*line); delete line; }
        return 0;
    }
    case WM_APP_INJECTED:
    {
        auto* complete = reinterpret_cast<InjectionComplete*>(lp);
        g_injecting = false;
        if (complete)
        {
            if (complete->pid == g_processId &&
                (g_launcherState == LauncherState::Injecting ||
                 g_launcherState == LauncherState::PostInjectionMonitoring))
            {
                if (complete->result.success)
                {
                    g_lastInjectedPid = complete->pid;
                    g_processId       = complete->pid;
                    g_gameOn          = true;
                    g_trainerLoaded   = true;
                    g_launcherState   = LauncherState::DiagnosticLoaded;
                    SaveTrainerCheatConfig();
                    ApplyDiagnosticCheatRestrictions();
                }
                else
                {
                    g_trainerLoaded = false;
                    g_launcherState = LauncherState::InjectionFailed;
                    if (complete->result.stage == L"PostMonitor")
                    {
                        g_processId             = 0;
                        g_gameOn                = false;
                        g_lastInjectedPid       = 0;
                        g_inDisappearGracePeriod = false;
                        g_pidDisappearTick      = 0;
                        g_gameDetectedTick      = 0;
                        g_pidStableTick         = 0;
                        ErrorLog(L"Diagnostic DLL caused Watch Dogs to close. See loader.log for details.");
                    }
                    else
                    {
                        std::wstring friendlyMsg;
                        if (complete->result.errorCode == 5 || 
                            complete->result.message.find(L"Access is denied") != std::wstring::npos ||
                            complete->result.message.find(L"Access Denied") != std::wstring::npos)
                        {
                            friendlyMsg = L"Injection failed: Access denied. Run the trainer as administrator after Watch Dogs is fully loaded.";
                        }
                        else
                        {
                            friendlyMsg = L"Injection failed. See loader.log for details.";
                        }
                        ErrorLog(friendlyMsg);
                    }
                    LoaderLog::Write(L"Injection failed with stage: " + complete->result.stage + L", errorCode: " + std::to_wstring(complete->result.errorCode) + L", message: " + complete->result.message);
                    ApplyDiagnosticCheatRestrictions();
                }
            }
            delete complete;
        }
        InvalidateMain();
        return 0;
    }

    // ── Edit-box colour ───────────────────────────────────────────────────────
    case WM_CTLCOLORBTN:
    {
        return reinterpret_cast<LRESULT>(g_panelBrush);
    }
    case WM_CTLCOLOREDIT:
    {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc,   RGB(7, 9, 10));
        SetTextColor(dc, RGB(255, 105, 105));
        return reinterpret_cast<LRESULT>(g_errorBrush);
    }
    case WM_CTLCOLORSTATIC:
    {
        if (reinterpret_cast<HWND>(lp) == g_errorBox)
        {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc,   RGB(7, 9, 10));
            SetTextColor(dc, RGB(255, 105, 105));
            return reinterpret_cast<LRESULT>(g_errorBrush);
        }
        break;
    }

    // ── Owner-draw button ─────────────────────────────────────────────────────
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;

    // ── Suppress background erase (double-buffered) ───────────────────────────
    case WM_ERASEBKGND:
        return 1;

    // ── Double-buffered paint ─────────────────────────────────────────────────
    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rcc{};
        GetClientRect(hwnd, &rcc);
        HDC     memDc = CreateCompatibleDC(hdc);
        HBITMAP memBm = CreateCompatibleBitmap(hdc,
            rcc.right - rcc.left, rcc.bottom - rcc.top);
        HGDIOBJ oldBm = SelectObject(memDc, memBm);
        PaintMain(memDc, hwnd);
        BitBlt(hdc, 0, 0, rcc.right - rcc.left, rcc.bottom - rcc.top,
               memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Destroy ───────────────────────────────────────────────────────────────
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_SCAN);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// GDI resource management
// ---------------------------------------------------------------------------
static void CreateGuiResources()
{
    g_bgBrush    = CreateSolidBrush(C_BG);
    g_panelBrush = CreateSolidBrush(C_PANEL);
    g_errorBrush = CreateSolidBrush(RGB(7, 9, 10));

    auto MkFont = [](int h, int weight, const wchar_t* face) -> HFONT
    {
        return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };

    g_titleFont    = MkFont(40, FW_BOLD,     L"Segoe UI");
    g_subtitleFont = MkFont(21, FW_SEMIBOLD, L"Segoe UI");
    g_bodyFont     = MkFont(16, FW_NORMAL,   L"Segoe UI");
    g_smallFont    = MkFont(14, FW_NORMAL,   L"Segoe UI");
    g_tinyFont     = MkFont(13, FW_NORMAL,   L"Segoe UI");
    g_logFont      = MkFont(13, FW_NORMAL,   L"Consolas");
}

static void DestroyGuiResources()
{
    delete g_backgroundImage; g_backgroundImage = nullptr;
    delete g_logoImage;       g_logoImage       = nullptr;

    HFONT fonts[] = { g_titleFont, g_subtitleFont, g_bodyFont,
                      g_smallFont, g_tinyFont,     g_logFont };
    for (auto* f : fonts) if (f) DeleteObject(f);
    g_titleFont = g_subtitleFont = g_bodyFont =
    g_smallFont = g_tinyFont    = g_logFont  = nullptr;

    DeleteObject(g_bgBrush);
    DeleteObject(g_panelBrush);
    DeleteObject(g_errorBrush);
    if (g_gdiplusToken) { GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // 1. Single-instance protection
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MenyooWatchDogsLoaderMutex");
    if (hMutex == nullptr)
    {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        HWND existing = nullptr;
        for (int i = 0; i < 10; ++i)
        {
            existing = FindWindowW(WINDOW_CLASS, nullptr);
            if (existing) break;
            Sleep(50);
        }
        if (existing)
        {
            if (IsIconic(existing))
                ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    g_exePath = GetExecutablePath();
    g_exeDir  = g_exePath.parent_path();
    g_dllPath = g_exeDir / L"WatchDogsTrainer.dll";

    GdiplusStartupInput gsi{};
    GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);
    LoadLauncherImages();

    g_launcherConfigPath = GetAppDataPath(L"launcher.cfg");
    g_trainerConfigPath  = GetAppDataPath(L"WatchDogsTrainer.json");
    g_diagConfigPath     = g_dllPath.parent_path() / L"trainer_debug.json";
    LoadLauncherConfig();
    InitCheats();
    LoadTrainerCheatConfig();
    LoadDiagnosticConfig();

    g_logFile.open(g_exeDir / L"loader.log", std::ios::app);
    LoaderLog::Write(L"Loader started.");
    LoaderLog::Write(L"DLL path: " + g_dllPath.wstring());
    if (!std::filesystem::exists(g_dllPath))
        ErrorLog(L"DLL missing: " + g_dllPath.wstring());

    const std::wstring loaderArch = (sizeof(void*) == 8) ? L"x64" : L"x86";
    const std::wstring dllArch    = DescribeBinaryArchitecture(g_dllPath);
    const std::wstring gameArch   = g_gameExePath.empty() ? L"(unknown)" : DescribeBinaryArchitecture(g_gameExePath);
    LoaderLog::Write(L"Architecture: Loader=" + loaderArch + L", Trainer DLL=" + dllArch + L", Game=" + gameArch);

    CreateGuiResources();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = nullptr;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIconSm       = nullptr;
    RegisterClassExW(&wc);

    DWORD style   = WS_POPUP | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW;

    RECT desired{ 0, 0, 1140, 900 };
    AdjustWindowRectEx(&desired, style, FALSE, exStyle);

    g_hWnd = CreateWindowExW(exStyle, WINDOW_CLASS, WINDOW_TITLE,
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hWnd)
    {
        LoaderLog::WriteLastError(L"CreateWindowExW failed", GetLastError());
        DestroyGuiResources();
        CloseHandle(hMutex);
        return 1;
    }

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
    if (g_gameDir.empty()) PromptForGamePath(g_hWnd);
    ScanForGame();

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0))
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyGuiResources();
    CloseHandle(hMutex);
    return 0;
}
