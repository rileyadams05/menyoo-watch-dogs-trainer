#include "menu.h"
#include "ui.h"
#include "../common.h"
#include "../cheats/cheat_manager.h"
#include "../hooks/hooks.h"
#include "../hooks/dx11_hook.h"
#include "../process/process.h"
#include "../memory/pointer_chain.h"
#include "../memory/memory.h"
#include "../config/config.h"
#include "../diagnostics/diagnostics.h"

#include "imgui.h"

// -----------------------------------------------------------------------
// Menyoo-inspired colour palette
// -----------------------------------------------------------------------
static const ImVec4 COL_BG          = ImVec4(0.06f, 0.06f, 0.08f, 0.97f);
static const ImVec4 COL_HEADER      = ImVec4(0.10f, 0.61f, 0.98f, 1.00f);
static const ImVec4 COL_HEADER_ACT  = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
static const ImVec4 COL_ACCENT      = ImVec4(0.00f, 0.60f, 1.00f, 1.00f);
static const ImVec4 COL_ON          = ImVec4(0.15f, 0.85f, 0.30f, 1.00f);
static const ImVec4 COL_OFF         = ImVec4(0.75f, 0.15f, 0.15f, 1.00f);
static const ImVec4 COL_TITLE_BG    = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
static const ImVec4 COL_TAB_ACT     = ImVec4(0.10f, 0.40f, 0.80f, 1.00f);
static const ImVec4 COL_TAB_IDLE    = ImVec4(0.08f, 0.20f, 0.40f, 1.00f);
static const ImVec4 COL_SEPARATOR   = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
static const ImVec4 COL_TEXT        = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
static const ImVec4 COL_TEXT_DIM    = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
static const ImVec4 COL_BTN         = ImVec4(0.12f, 0.35f, 0.65f, 1.00f);
static const ImVec4 COL_BTN_HOV     = ImVec4(0.18f, 0.48f, 0.82f, 1.00f);
static const ImVec4 COL_BTN_ACT     = ImVec4(0.08f, 0.28f, 0.55f, 1.00f);

namespace
{
    std::string g_captureCheatId;
    std::string g_captureCheatName;
    std::string g_duplicateCheatId;
    UINT g_pendingVk = 0;

    std::string VirtualKeyName(UINT vk)
    {
        if (vk == 0)
            return "None";

        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LONG lParamValue = static_cast<LONG>(sc) << 16;

        switch (vk)
        {
        case VK_LWIN:
        case VK_RWIN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_INSERT:
        case VK_DELETE:
        case VK_PRIOR:
        case VK_NEXT:
            lParamValue |= 0x01000000;
            break;
        default:
            break;
        }

        wchar_t name[64] = {};
        if (GetKeyNameTextW(lParamValue, name, static_cast<int>(_countof(name))) > 0)
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0)
            {
                std::string result(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), len - 1, nullptr, nullptr);
                return result;
            }
        }

        char fallback[16] = {};
        snprintf(fallback, sizeof(fallback), "VK_%02X", vk);
        return fallback;
    }

    UINT PollPressedVirtualKey()
    {
        for (UINT vk = 1; vk < 255; ++vk)
        {
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON)
                continue;

            if (GetAsyncKeyState(static_cast<int>(vk)) & 0x0001)
                return vk;
        }

        return 0;
    }
}

void Menu::ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 6.0f;
    s.FrameRounding     = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;
    s.ChildRounding     = 4.0f;
    s.PopupRounding     = 4.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    s.FramePadding      = ImVec2(10.0f, 5.0f);
    s.WindowPadding     = ImVec2(12.0f, 10.0f);
    s.ScrollbarSize     = 10.0f;
    s.IndentSpacing     = 20.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]           = COL_BG;
    c[ImGuiCol_ChildBg]            = ImVec4(0.07f, 0.07f, 0.10f, 1.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.06f, 0.06f, 0.09f, 0.98f);
    c[ImGuiCol_Border]             = ImVec4(0.20f, 0.20f, 0.28f, 0.80f);
    c[ImGuiCol_TitleBg]            = COL_TITLE_BG;
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.06f, 0.25f, 0.55f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.04f, 0.04f, 0.06f, 0.75f);
    c[ImGuiCol_Header]             = ImVec4(0.10f, 0.35f, 0.70f, 0.55f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.12f, 0.42f, 0.82f, 0.80f);
    c[ImGuiCol_HeaderActive]       = COL_HEADER_ACT;
    c[ImGuiCol_Button]             = COL_BTN;
    c[ImGuiCol_ButtonHovered]      = COL_BTN_HOV;
    c[ImGuiCol_ButtonActive]       = COL_BTN_ACT;
    c[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.16f, 0.16f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.18f, 0.18f, 0.28f, 1.00f);
    c[ImGuiCol_Tab]                = COL_TAB_IDLE;
    c[ImGuiCol_TabHovered]         = COL_TAB_ACT;
    c[ImGuiCol_TabActive]          = COL_TAB_ACT;
    c[ImGuiCol_TabUnfocused]       = COL_TAB_IDLE;
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.08f, 0.30f, 0.65f, 1.00f);
    c[ImGuiCol_Separator]          = COL_SEPARATOR;
    c[ImGuiCol_SeparatorHovered]   = COL_ACCENT;
    c[ImGuiCol_SeparatorActive]    = COL_ACCENT;
    c[ImGuiCol_SliderGrab]         = COL_ACCENT;
    c[ImGuiCol_SliderGrabActive]   = COL_HEADER;
    c[ImGuiCol_CheckMark]          = COL_ON;
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.22f, 0.22f, 0.32f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.30f, 0.30f, 0.45f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = COL_ACCENT;
    c[ImGuiCol_Text]               = COL_TEXT;
    c[ImGuiCol_TextDisabled]       = COL_TEXT_DIM;
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.20f, 0.45f, 0.80f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]  = COL_ACCENT;
    c[ImGuiCol_ResizeGripActive]   = COL_HEADER;
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static void DrawCheatControl(Cheat& cheat)
{
    ImGui::PushID(cheat.id.c_str());

    CheatManager& cm = CheatManager::Get();
    bool disabled = !cheat.available;

    if (disabled)
        ImGui::BeginDisabled();

    float rowHeight = ImGui::GetFrameHeightWithSpacing();
    float availW = ImGui::GetContentRegionAvail().x;

    ImGui::BeginGroup();

    if (cheat.momentary)
    {
        if (ImGui::Button(cheat.name.c_str(), ImVec2(availW, rowHeight)))
        {
            cm.SetActive(cheat.id, true);
        }
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(COL_TEXT_DIM, "(action)");
        if (!cheat.available)
        {
            ImGui::SameLine();
            ImGui::TextColored(COL_OFF, "(unavailable)");
        }
    }
    else
    {
        ImVec4 dotCol = cheat.active ? COL_ON : COL_OFF;
        ImGui::TextColored(dotCol, cheat.active ? "●" : "○");
        ImGui::SameLine(0.0f, 8.0f);

        bool active = cheat.active;
        ImGui::SetNextItemWidth(std::max(availW - 100.0f, 120.0f));
        std::string label = cheat.name + "##toggle";
        if (ImGui::Checkbox(label.c_str(), &active))
        {
            cm.SetActive(cheat.id, active);
            active = cheat.active;
            Config::Get().Save();
        }
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, cheat.active ? "ON" : "OFF");
    }

    if (!cheat.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", cheat.description.c_str());

    if (!cheat.failureId.empty() && Hooks::HasFailure(cheat.failureId))
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.2f, 1.0f), "(scan failed)");
    }

    if (disabled)
    {
        ImGui::SameLine();
        ImGui::TextColored(COL_OFF, "(unavailable)");
    }

    ImGui::EndGroup();

    if (disabled)
        ImGui::EndDisabled();

    ImGui::PopID();
}

// -----------------------------------------------------------------------
// Per-cheat extra controls (sliders, inputs shown below the toggle)
// -----------------------------------------------------------------------
static void DrawCheatExtras(Cheat& cheat)
{
    if (cheat.id == "overidefov")
    {
        float fov = HookFlags::fovValue.load();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("##fov", &fov, 40.0f, 120.0f, "FOV: %.0f"))
        {
            HookFlags::fovValue = fov;
            Config::Get().Save();
        }
    }
    else if (cheat.id == "settime")
    {
        float hrs = HookFlags::setTimeHours.load();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("##tod", &hrs, 0.0f, 23.9f, "Hour: %.1f"))
        {
            HookFlags::setTimeHours = hrs;
            Config::Get().Save();
        }
    }
    else if (cheat.id == "lockammo")
    {
        int maxA = HookFlags::maxAmmo.load();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Max Ammo##ammo", &maxA, 100, 1000))
        {
            if (maxA < 0) maxA = 0;
            HookFlags::maxAmmo = maxA;
            Config::Get().Save();
        }
    }
    else if (cheat.id == "lockcraft")
    {
        int maxC = HookFlags::maxCraftQty.load();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Max Qty##craft", &maxC, 10, 100))
        {
            if (maxC < 0) maxC = 0;
            HookFlags::maxCraftQty = maxC;
            Config::Get().Save();
        }
    }
    else if (cheat.id == "savecords" || cheat.id == "restorecords")
    {
        extern float g_savedX, g_savedY, g_savedZ;
        ImGui::TextColored(COL_TEXT_DIM, "  Saved: %.2f / %.2f / %.2f",
            g_savedX, g_savedY, g_savedZ);
    }
    else if (cheat.id == "noclip")
    {
        ImGui::TextColored(COL_TEXT_DIM,
            "  WASD = horizontal | E/Q = up/down | Mouse = look");
    }
}

// -----------------------------------------------------------------------
// Optional per-cheat hotkeys. Defaults are intentionally empty.
// -----------------------------------------------------------------------
static void AssignCheatHotkey(const std::string& cheatId, UINT vk, bool overrideDuplicate)
{
    Config& cfg = Config::Get();
    std::string duplicate = cfg.FindCheatByHotkey(vk, cheatId);

    if (!duplicate.empty() && !overrideDuplicate)
    {
        g_pendingVk = vk;
        g_duplicateCheatId = duplicate;
        ImGui::OpenPopup("Duplicate Hotkey");
        return;
    }

    if (!duplicate.empty())
        cfg.ClearCheatHotkey(duplicate);

    cfg.SetCheatHotkey(cheatId, vk);
    cfg.Save();
    g_captureCheatId.clear();
    g_captureCheatName.clear();
    g_duplicateCheatId.clear();
    g_pendingVk = 0;
}

static void DrawKeybindsPanel()
{
    Config& cfg = Config::Get();
    CheatManager& cm = CheatManager::Get();

    ImGui::TextColored(COL_TEXT_DIM, "Cheat hotkeys are optional. Unassigned cheats remain controllable from the menu.");
    ImGui::Spacing();

    if (ImGui::Button("Clear All Hotkeys", ImVec2(170.0f, 28.0f)))
    {
        cfg.ClearAllCheatHotkeys();
        cfg.Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Menu Hotkey", ImVec2(170.0f, 28.0f)))
    {
        cfg.SetMenuHotkey(VK_LWIN);
        UI::SetMenuHotkey(VK_LWIN);
        cfg.Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Menu opens with Left Windows. Insert remains the backup fallback.");

    ImGui::Separator();

    if (!g_captureCheatId.empty())
    {
        ImGui::TextColored(COL_ACCENT, "Press a key for %s. Esc cancels. Backspace clears.",
            g_captureCheatName.c_str());

        UINT vk = g_pendingVk == 0 ? PollPressedVirtualKey() : 0;
        if (vk == VK_ESCAPE)
        {
            g_captureCheatId.clear();
            g_captureCheatName.clear();
        }
        else if (vk == VK_BACK || vk == VK_DELETE)
        {
            cfg.ClearCheatHotkey(g_captureCheatId);
            cfg.Save();
            g_captureCheatId.clear();
            g_captureCheatName.clear();
        }
        else if (vk != 0)
        {
            AssignCheatHotkey(g_captureCheatId, vk, false);
        }
    }
    else
    {
        ImGui::TextColored(COL_TEXT_DIM, "Select Assign or Change, then press the desired key.");
    }

    if (ImGui::BeginPopupModal("Duplicate Hotkey", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const Cheat* duplicate = cm.Find(g_duplicateCheatId);
        std::string duplicateName = duplicate ? duplicate->name : g_duplicateCheatId;
        ImGui::TextWrapped("%s is already assigned to %s.",
            VirtualKeyName(g_pendingVk).c_str(), duplicateName.c_str());
        ImGui::TextWrapped("Override that assignment?");
        ImGui::Spacing();

        if (ImGui::Button("Override", ImVec2(120.0f, 28.0f)))
        {
            AssignCheatHotkey(g_captureCheatId, g_pendingVk, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 28.0f)))
        {
            g_pendingVk = 0;
            g_duplicateCheatId.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (ImGui::BeginChild("##keybind_rows", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        for (const auto& cheat : cm.GetCheats())
        {
            ImGui::PushID(cheat.id.c_str());

            UINT vk = cfg.GetCheatHotkey(cheat.id);
            ImGui::TextUnformatted(cheat.name.c_str());
            ImGui::SameLine(260.0f);
            ImGui::TextColored(vk ? COL_ACCENT : COL_TEXT_DIM, "%s", VirtualKeyName(vk).c_str());
            ImGui::SameLine(390.0f);

            if (ImGui::Button(vk ? "Change" : "Assign", ImVec2(82.0f, 24.0f)))
            {
                g_captureCheatId = cheat.id;
                g_captureCheatName = cheat.name;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(70.0f, 24.0f)))
            {
                cfg.ClearCheatHotkey(cheat.id);
                cfg.Save();
                if (g_captureCheatId == cheat.id)
                {
                    g_captureCheatId.clear();
                    g_captureCheatName.clear();
                }
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
}

// -----------------------------------------------------------------------
// Player coordinate display panel
// -----------------------------------------------------------------------
static void DrawCoordPanel()
{
    if (!g_ptrs.pCoord) return;
    uptr coordPtr = Memory::Read<uptr>(g_ptrs.pCoord);
    if (!coordPtr) return;

    float x = 0, y = 0, z = 0;
    Memory::SafeRead<float>(coordPtr + 0x0, x);
    Memory::SafeRead<float>(coordPtr + 0x4, y);
    Memory::SafeRead<float>(coordPtr + 0x8, z);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.08f, 0.14f, 1.0f));
    ImGui::BeginChild("##coords", ImVec2(0, 58), true);
    ImGui::TextColored(COL_TEXT_DIM, "Player Position");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
    ImGui::Text("X: %8.2f   Y: %8.2f   Z: %8.2f", x, y, z);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// -----------------------------------------------------------------------
// Main menu window
// -----------------------------------------------------------------------
void Menu::Draw()
{
    CheatManager& cm = CheatManager::Get();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(620.0f, 580.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::Begin("##WDTrainer", nullptr, wf);

    // ---- Title bar area ----
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_TITLE_BG);
        ImGui::BeginChild("##title_bar", ImVec2(0, 52), false);

        ImGui::SetCursorPosY(6.0f);
        ImGui::SetCursorPosX(14.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::Text("WATCH DOGS");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(COL_TEXT_DIM, "Trainer  v1.0");

        const UINT primaryHotkey = Config::Get().GetMenuHotkey();
        const bool hasFallback = primaryHotkey != VK_INSERT;
        const std::string primaryName = VirtualKeyName(primaryHotkey);
        const std::string fallbackName = VirtualKeyName(VK_INSERT);

        ImGui::SetCursorPosY(28.0f);
        ImGui::SetCursorPosX(14.0f);
        if (hasFallback)
        {
            ImGui::TextColored(
                COL_TEXT_DIM,
                "%s (or %s) = Toggle Menu  |  Single-player only",
                primaryName.c_str(), fallbackName.c_str());
        }
        else
        {
            ImGui::TextColored(
                COL_TEXT_DIM,
                "%s = Toggle Menu  |  Single-player only",
                primaryName.c_str());
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // ---- Activate All / Deactivate All buttons ----
    {
        float bw = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
        if (ImGui::Button("Activate All", ImVec2(bw, 26.0f)))
        {
            cm.ActivateAll();
            Config::Get().SaveAll(cm.GetCheats());
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (ImGui::Button("Deactivate All", ImVec2(bw, 26.0f)))
        {
            cm.DeactivateAll();
            Config::Get().SaveAll(cm.GetCheats());
        }
    }

    ImGui::Separator();

    // ---- Coordinate display ----
    DrawCoordPanel();

    // Status panel
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.12f, 0.9f));
    ImGui::BeginChild("##status_panel", ImVec2(0, 120), true);

    auto drawStatus = [](const char* label, bool ok)
    {
        ImGui::TextColored(ok ? COL_ON : COL_OFF, ok ? "●" : "○");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(COL_TEXT_DIM, "%s", label);
    };

    drawStatus("Game detected", g_baseGame != 0);
    drawStatus("Disrupt loaded", g_baseDisrupt != 0);
    drawStatus("DX11 hook", DX11Hook::IsReady());
    drawStatus("Hooks initialized", Hooks::IsInitialized());
    drawStatus("Trainer running", Process::IsGameReady());

    auto failures = Hooks::GetFailures();
    if (!failures.empty())
    {
        ImGui::Separator();
        ImGui::TextColored(COL_OFF, "Failed hooks");
        for (const auto& f : failures)
        {
            ImGui::Bullet();
            ImGui::TextColored(COL_TEXT_DIM, "%s - %s", f.id.c_str(), f.reason.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Spacing();

    // ---- Tab bar (one tab per category) ----
    auto categories = cm.GetCategories();

    if (ImGui::BeginTabBar("##tabs",
        ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
    {
        for (const auto& cat : categories)
        {
            if (ImGui::BeginTabItem(cat.c_str()))
            {
                auto cheats = cm.GetByCategory(cat);

                ImGui::BeginChild(
                    ("##scroll_" + cat).c_str(),
                    ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

                for (Cheat* cheat : cheats)
                {
                    DrawCheatControl(*cheat);
                    DrawCheatExtras(*cheat);
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }

        if (ImGui::BeginTabItem("Keybinds"))
        {
            ImGui::BeginChild("##scroll_keybinds", ImVec2(0, 0), false,
                ImGuiWindowFlags_HorizontalScrollbar);
            DrawKeybindsPanel();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// -----------------------------------------------------------------------
// Small always-visible status bar (top-right corner)
// -----------------------------------------------------------------------
void Menu::DrawStatusBar()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f, 10.0f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(180.0f, 40.0f));
    ImGui::SetNextWindowBgAlpha(0.70f);

    ImGuiWindowFlags sf =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##statusbar", nullptr, sf);

    // Count active cheats
    int active = 0;
    for (const auto& c : CheatManager::Get().GetCheats())
        if (c.active) ++active;

    const auto& diag = Diagnostics::GetConfig();

    ImGui::TextColored(COL_ACCENT, diag.diagnosticMode ? "WD Trainer (Diagnostic)" : "WD Trainer");
    ImGui::SameLine();
    if (diag.enableCheats && active > 0)
        ImGui::TextColored(COL_ON, "(%d active)", active);
    else if (diag.enableCheats)
        ImGui::TextColored(COL_TEXT_DIM, "(idle)");
    else
        ImGui::TextColored(COL_TEXT_DIM, "Cheats disabled");

    if (diag.enableOverlay)
    {
        const UINT primaryHotkey = Config::Get().GetMenuHotkey();
        const bool hasFallback = primaryHotkey != VK_INSERT;
        const std::string primaryName = VirtualKeyName(primaryHotkey);
        const std::string fallbackName = VirtualKeyName(VK_INSERT);

        if (diag.enableHotkeys)
        {
            if (hasFallback)
                ImGui::TextColored(COL_TEXT_DIM, "%s or %s to open menu", primaryName.c_str(), fallbackName.c_str());
            else
                ImGui::TextColored(COL_TEXT_DIM, "%s to open menu", primaryName.c_str());
        }
        else
        {
            ImGui::TextColored(COL_TEXT_DIM, "Hotkeys disabled in diagnostic mode");
        }
    }
    else
    {
        ImGui::TextColored(COL_TEXT_DIM, "Overlay disabled");
    }

    ImGui::End();
}
