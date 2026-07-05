#include "ui.h"
#include "menu.h"
#include "../common.h"
#include "../cheats/cheat_manager.h"
#include "../config/config.h"
#include "../diagnostics/diagnostics.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static bool      g_menuOpen     = false;
static bool      g_initialized  = false;
static HWND      g_hwnd         = nullptr;
static WNDPROC   g_origWndProc  = nullptr;
static UINT      g_menuHotkey   = VK_LWIN;
static constexpr UINT kFallbackHotkey = VK_INSERT;

namespace
{
    bool IsToggleMessage(UINT msg)
    {
        return msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    }

    bool IsKeyUpMessage(UINT msg)
    {
        return msg == WM_KEYUP || msg == WM_SYSKEYUP;
    }

    bool IsInitialPress(LPARAM lParam)
    {
        return (lParam & (1 << 30)) == 0;
    }

    bool IsToggleVirtualKey(WPARAM wParam)
    {
        const UINT vk = static_cast<UINT>(wParam);
        if (vk == g_menuHotkey)
            return true;
        if (vk == kFallbackHotkey && g_menuHotkey != kFallbackHotkey)
            return true;
        return false;
    }
}

// -----------------------------------------------------------------------
LRESULT CALLBACK UI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const auto& diag = Diagnostics::GetConfig();
    const bool allowMenuToggle = diag.enableOverlay;
    const bool allowHotkeys    = diag.enableHotkeys && diag.enableCheats;

    if (allowMenuToggle && IsToggleMessage(msg) && IsToggleVirtualKey(wParam))
    {
        if (IsInitialPress(lParam))
            ToggleMenu();
        return 0;
    }

    if (allowHotkeys && !g_menuOpen && IsToggleMessage(msg) && IsInitialPress(lParam) && GetForegroundWindow() == hwnd)
    {
        if (CheatManager::Get().HandleHotkey(static_cast<UINT>(wParam)))
            return 0;
    }

    if (allowMenuToggle && IsKeyUpMessage(msg) && static_cast<UINT>(wParam) == g_menuHotkey)
    {
        return 0;
    }

    if (allowMenuToggle && g_menuOpen)
    {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

        // Block mouse input from reaching the game when menu is open
        if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
            msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_MOUSEWHEEL)
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse) return 0;
        }
    }

    return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------
void UI::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd)
{
    if (g_initialized) return;
    g_hwnd = hwnd;

    UINT configuredHotkey = Config::Get().GetMenuHotkey();
    if (configuredHotkey == 0)
        configuredHotkey = VK_LWIN;
    g_menuHotkey = configuredHotkey;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // don't create imgui.ini in game directory

    // Style — Menyoo-inspired dark theme
    Menu::ApplyStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);

    // Subclass the game's window to intercept WM_KEYDOWN
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(UI::WndProc)));

    g_initialized = true;
    LOG("ImGui UI initialized");
}

// -----------------------------------------------------------------------
void UI::Render()
{
    if (!g_initialized) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_menuOpen)
        Menu::Draw();

    // Always show a small status indicator in the corner
    Menu::DrawStatusBar();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

// -----------------------------------------------------------------------
void UI::OnResize(UINT /*width*/, UINT /*height*/)
{
    // Nothing special needed — ImGui handles resize automatically
}

void UI::Shutdown()
{
    if (!g_initialized) return;
    if (g_hwnd && g_origWndProc)
        SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(g_origWndProc));

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
}

bool UI::IsMenuOpen()          { return g_menuOpen; }
void UI::SetMenuOpen(bool open){ g_menuOpen = open; }
void UI::ToggleMenu()          { g_menuOpen = !g_menuOpen; }
void UI::SetMenuHotkey(UINT vk){ g_menuHotkey = vk ? vk : VK_LWIN; }
