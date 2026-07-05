#pragma once
#include "../common.h"
#include <string>
#include <unordered_map>
#include <vector>

struct Cheat;

class Config
{
public:
    static Config& Get();

    void Load();
    void Save();
    void ReloadIfModified();

    void SetCheat(const std::string& id, bool active);
    bool GetCheat(const std::string& id) const;

    void SaveAll(const std::vector<Cheat>& cheats);
    void ApplyAll(std::vector<Cheat>& cheats) const;

    void SetMenuHotkey(UINT vk);
    UINT GetMenuHotkey() const;

    void SetCheatHotkey(const std::string& id, UINT vk);
    UINT GetCheatHotkey(const std::string& id) const;
    void ClearCheatHotkey(const std::string& id);
    void ClearAllCheatHotkeys();
    std::string FindCheatByHotkey(UINT vk, const std::string& exceptId = "") const;

    // Scalar settings
    float fov         = 75.0f;
    float timeHours   = 12.0f;
    int   maxAmmo     = 9999;
    int   maxCraft    = 999;

private:
    Config() = default;
    std::string GetConfigPath() const;
    std::unordered_map<std::string, bool> m_cheatStates;
    std::unordered_map<std::string, UINT> m_cheatHotkeys;
    UINT m_menuHotkey = VK_LWIN;
};
