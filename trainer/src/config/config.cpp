#include "config.h"
#include "../cheats/cheat_manager.h"
#include "../hooks/hooks.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <ShlObj.h>
#include <filesystem>

using json = nlohmann::json;

// -----------------------------------------------------------------------
Config& Config::Get()
{
    static Config inst;
    return inst;
}

// -----------------------------------------------------------------------
std::string Config::GetConfigPath() const
{
    char appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, appData)))
    {
        std::string dir = std::string(appData) + "\\MENYOO Watch Dogs Trainer";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\WatchDogsTrainer.json";
    }

    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    char* last = strrchr(path, '\\');
    if (last) *(last + 1) = '\0';

    strcat_s(path, "WatchDogsTrainer.json");
    return path;
}

// -----------------------------------------------------------------------
void Config::Load()
{
    std::ifstream f(GetConfigPath());
    if (!f.is_open()) return;

    try
    {
        json j;
        f >> j;

        if (j.contains("cheats") && j["cheats"].is_object())
            for (auto& [k, v] : j["cheats"].items())
                m_cheatStates[k] = v.get<bool>();

        if (j.contains("fov"))         fov         = j["fov"].get<float>();
        if (j.contains("timeHours"))   timeHours   = j["timeHours"].get<float>();
        if (j.contains("maxAmmo"))     maxAmmo     = j["maxAmmo"].get<int>();
        if (j.contains("maxCraft"))    maxCraft    = j["maxCraft"].get<int>();
        if (j.contains("menuHotkey"))  m_menuHotkey= j["menuHotkey"].get<UINT>();
        if (j.contains("hotkeys") && j["hotkeys"].is_object())
        {
            for (auto& [k, v] : j["hotkeys"].items())
            {
                UINT vk = v.get<UINT>();
                if (vk != 0)
                    m_cheatHotkeys[k] = vk;
            }
        }

        // Apply to HookFlags
        HookFlags::fovValue    = fov;
        HookFlags::setTimeHours= timeHours;
        HookFlags::maxAmmo     = maxAmmo;
        HookFlags::maxCraftQty = maxCraft;
    }
    catch (...) {}
}

// -----------------------------------------------------------------------
void Config::Save()
{
    json j;
    j["cheats"] = json::object();
    for (const auto& [k, v] : m_cheatStates)
        j["cheats"][k] = v;

    j["fov"]        = HookFlags::fovValue.load();
    j["timeHours"]  = HookFlags::setTimeHours.load();
    j["maxAmmo"]    = HookFlags::maxAmmo.load();
    j["maxCraft"]   = HookFlags::maxCraftQty.load();
    j["menuHotkey"] = m_menuHotkey;
    j["hotkeys"] = json::object();
    for (const auto& [k, v] : m_cheatHotkeys)
        if (v != 0)
            j["hotkeys"][k] = v;

    std::ofstream f(GetConfigPath());
    if (f.is_open()) f << j.dump(2);
}

// -----------------------------------------------------------------------
void Config::SetCheat(const std::string& id, bool active)
{
    m_cheatStates[id] = active;
}

bool Config::GetCheat(const std::string& id) const
{
    auto it = m_cheatStates.find(id);
    return it != m_cheatStates.end() ? it->second : false;
}

// -----------------------------------------------------------------------
void Config::SaveAll(const std::vector<Cheat>& cheats)
{
    for (const auto& c : cheats)
    {
        if (c.momentary) continue;
        m_cheatStates[c.id] = c.active;
    }
    Save();
}

void Config::ApplyAll(std::vector<Cheat>& cheats) const
{
    auto& mgr = CheatManager::Get();
    for (auto& c : cheats)
    {
        if (c.momentary) continue;
        auto it = m_cheatStates.find(c.id);
        if (it != m_cheatStates.end())
            mgr.SetActive(c.id, it->second);
    }
}

void Config::SetMenuHotkey(UINT vk)
{
    m_menuHotkey = vk;
}

UINT Config::GetMenuHotkey() const
{
    return m_menuHotkey;
}

void Config::SetCheatHotkey(const std::string& id, UINT vk)
{
    if (vk == 0)
        m_cheatHotkeys.erase(id);
    else
        m_cheatHotkeys[id] = vk;
}

UINT Config::GetCheatHotkey(const std::string& id) const
{
    auto it = m_cheatHotkeys.find(id);
    return it != m_cheatHotkeys.end() ? it->second : 0;
}

void Config::ClearCheatHotkey(const std::string& id)
{
    m_cheatHotkeys.erase(id);
}

void Config::ClearAllCheatHotkeys()
{
    m_cheatHotkeys.clear();
}

std::string Config::FindCheatByHotkey(UINT vk, const std::string& exceptId) const
{
    if (vk == 0)
        return "";

    for (const auto& [id, assignedVk] : m_cheatHotkeys)
    {
        if (assignedVk == vk && id != exceptId)
            return id;
    }

    return "";
}

void Config::ReloadIfModified()
{
    static DWORD lastCheck = 0;
    DWORD now = GetTickCount();
    if (now - lastCheck < 1000) return;
    lastCheck = now;

    std::string path = GetConfigPath();
    if (path.empty() || !std::filesystem::exists(path)) return;

    static std::filesystem::file_time_type lastTime;
    static bool hasLastTime = false;

    try
    {
        auto curTime = std::filesystem::last_write_time(path);
        if (!hasLastTime || curTime > lastTime)
        {
            lastTime = curTime;
            hasLastTime = true;
            Load();

            // Sync values to active cheats
            auto& mgr = CheatManager::Get();
            for (auto& c : mgr.GetCheats())
            {
                if (c.momentary) continue;
                auto it = m_cheatStates.find(c.id);
                if (it != m_cheatStates.end())
                {
                    mgr.SetActive(c.id, it->second);
                }
            }
        }
    }
    catch (...) {}
}
