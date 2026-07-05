#pragma once
#include "../common.h"
#include <string>
#include <vector>
#include <functional>

// -----------------------------------------------------------------------
// Cheat descriptor — one entry per toggle in the UI
// -----------------------------------------------------------------------
struct Cheat
{
    std::string id;           // stable key for config save/load
    std::string name;         // display name
    std::string category;     // category label for grouping
    std::string description;  // tooltip text
    std::string failureId;    // hook failure identifier
    bool        active = false;
    bool        available = true;      // false if AOB scan failed
    bool        momentary = false;     // true = acts like a button
    bool        safe = true;           // included in Activate All if true
    std::vector<std::string> conflicts; // ids that cannot be active together

    std::function<void(bool)> onToggle;  // called when active changes
    std::function<void()>     onTick;    // called every frame while active
};

// -----------------------------------------------------------------------
// Cheat categories (menu tabs)
// -----------------------------------------------------------------------
namespace Category
{
    constexpr const char* Player     = "Player";
    constexpr const char* Weapons    = "Weapons";
    constexpr const char* Vehicles   = "Vehicles";
    constexpr const char* World      = "World";
    constexpr const char* Movement   = "Movement";
    constexpr const char* DigitalTrip= "Digital Trips";
    constexpr const char* Timers     = "Timers";
    constexpr const char* Misc       = "Misc";
}

// -----------------------------------------------------------------------
class CheatManager
{
public:
    static CheatManager& Get();

    void RegisterAll();
    void Tick();           // called every frame

    void SetActive(const std::string& id, bool active);
    bool HandleHotkey(UINT vk);
    bool IsActive(const std::string& id) const;
    void ActivateAll();
    void DeactivateAll();

    Cheat*       Find(const std::string& id);
    const Cheat* Find(const std::string& id) const;

    const std::vector<Cheat>& GetCheats() const { return m_cheats; }
    std::vector<Cheat>& GetCheats() { return m_cheats; }

    std::vector<std::string> GetCategories() const;
    std::vector<Cheat*> GetByCategory(const std::string& cat);

private:
    CheatManager() = default;
    std::vector<Cheat> m_cheats;

    void Register(Cheat c) { m_cheats.push_back(std::move(c)); }
};
