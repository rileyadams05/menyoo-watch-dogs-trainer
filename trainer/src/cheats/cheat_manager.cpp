#include "cheat_manager.h"
#include "noclip.h"
#include "teleport.h"
#include "../hooks/hooks.h"
#include "../config/config.h"
#include "../memory/pointer_chain.h"
#include "../memory/memory.h"
#include <utility>

// -----------------------------------------------------------------------
CheatManager& CheatManager::Get()
{
    static CheatManager inst;
    return inst;
}

// -----------------------------------------------------------------------
Cheat* CheatManager::Find(const std::string& id)
{
    for (auto& c : m_cheats)
        if (c.id == id) return &c;
    return nullptr;
}

const Cheat* CheatManager::Find(const std::string& id) const
{
    for (const auto& c : m_cheats)
        if (c.id == id) return &c;
    return nullptr;
}

void CheatManager::SetActive(const std::string& id, bool active)
{
    Cheat* cheat = Find(id);
    if (!cheat || !cheat->available) return;

    if (cheat->momentary)
    {
        if (active && cheat->onToggle) cheat->onToggle(true);
        return;
    }

    if (cheat->active == active) return;

    if (active)
    {
        for (const auto& conflictId : cheat->conflicts)
        {
            if (IsActive(conflictId))
            {
                LOG("[CheatManager] Cannot enable %s because %s is active", id.c_str(), conflictId.c_str());
                return;
            }
        }
    }

    cheat->active = active;
    if (cheat->onToggle) cheat->onToggle(active);
    if (!cheat->momentary)
        Config::Get().SetCheat(cheat->id, cheat->active);
}

bool CheatManager::HandleHotkey(UINT vk)
{
    std::string id = Config::Get().FindCheatByHotkey(vk);
    if (id.empty())
        return false;

    Cheat* cheat = Find(id);
    if (!cheat || !cheat->available)
        return false;

    if (cheat->momentary)
    {
        SetActive(id, true);
    }
    else
    {
        SetActive(id, !cheat->active);
        Config::Get().Save();
    }

    return true;
}

bool CheatManager::IsActive(const std::string& id) const
{
    const Cheat* cheat = Find(id);
    return cheat ? cheat->active : false;
}

void CheatManager::ActivateAll()
{
    for (auto& c : m_cheats)
    {
        if (!c.available || c.momentary) continue;
        SetActive(c.id, true);
    }
}

void CheatManager::DeactivateAll()
{
    for (auto& c : m_cheats)
    {
        if (c.momentary) continue;
        SetActive(c.id, false);
    }
}

void CheatManager::Tick()
{
    Config::Get().ReloadIfModified();

    for (auto& c : m_cheats)
    {
        if (!c.failureId.empty())
            c.available = !Hooks::HasFailure(c.failureId);

        if (c.id == "savecords" || c.id == "restorecords" || c.id == "noclip")
            c.available = c.available && (g_ptrs.pCoord != 0);
        else if (c.id == "oneteleport")
            c.available = c.available && (g_ptrs.pMapWaypt != 0);

        if (c.active && !c.momentary && c.onTick)
            c.onTick();
    }
}

std::vector<std::string> CheatManager::GetCategories() const
{
    std::vector<std::string> cats;
    for (const auto& c : m_cheats)
    {
        bool found = false;
        for (const auto& cat : cats) if (cat == c.category) { found = true; break; }
        if (!found) cats.push_back(c.category);
    }
    return cats;
}

std::vector<Cheat*> CheatManager::GetByCategory(const std::string& cat)
{
    std::vector<Cheat*> result;
    for (auto& c : m_cheats)
        if (c.category == cat) result.push_back(&c);
    return result;
}

// -----------------------------------------------------------------------
// Per-frame helper: safely write a float to a resolved address
// -----------------------------------------------------------------------
static inline void SafeWriteFloat(uptr addr, float val)
{
    if (addr) Memory::SafeWrite<float>(addr, val);
}

static inline void SafeWriteInt(uptr addr, i32 val)
{
    if (addr) Memory::SafeWrite<i32>(addr, val);
}

static inline float SafeReadFloat(uptr addr, float def = 0.0f)
{
    float v = def;
    Memory::SafeRead<float>(addr, v);
    return v;
}

static inline i32 SafeReadInt(uptr addr, i32 def = 0)
{
    i32 v = def;
    Memory::SafeRead<i32>(addr, v);
    return v;
}

// -----------------------------------------------------------------------
// Resolve player stats base (Money/XP/Skills)
// Chain: pChainExp -> +0 -> +0x30 -> +0x450 -> +0x18 -> +0x2B0
// -----------------------------------------------------------------------
static uptr GetPlayerStatsBase()
{
    if (!g_ptrs.pChainExp) return 0;
    uptr base = Memory::Read<uptr>(g_ptrs.pChainExp);
    return Memory::ResolveChainSafe(base,
        { 0x30, 0x450, 0x18, 0x2B0, 0x0 });
}

static uptr GetPlayerHealthBase()
{
    // pHealth points to entity; health float at entity+0x18
    if (!g_ptrs.pHealth) return 0;
    uptr entity = Memory::Read<uptr>(g_ptrs.pHealth);
    if (!entity) return 0;
    return entity;
}

// -----------------------------------------------------------------------
void CheatManager::RegisterAll()
{
    if (!m_cheats.empty())
        return;

    const auto hasFailure = [](const std::string& id) {
        return !id.empty() && Hooks::HasFailure(id);
    };

    auto add = [&](Cheat&& c)
    {
        Register(std::move(c));
    };

    // ===================================================================
    // PLAYER
    // ===================================================================
    {
        Cheat c;
        c.id = "godmode";
        c.name = "God Mode";
        c.category = Category::Player;
        c.description = "Player cannot die. Health is locked at maximum.";
        c.failureId = "hook_godmode";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::godMode = on; };
        c.onTick = [&]() {
            uptr entity = g_ptrs.pHealth ? Memory::Read<uptr>(g_ptrs.pHealth) : 0;
            if (!entity) return;
            float hp = SafeReadFloat(entity + 0x18);
            float maxHp = SafeReadFloat(entity + 0x1C);
            if (hp < maxHp && maxHp > 0.0f)
                SafeWriteFloat(entity + 0x18, maxHp);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "inffocus";
        c.name = "Infinite Focus";
        c.category = Category::Player;
        c.description = "Focus bar never depletes.";
        c.failureId = "hook_focus";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::infiniteFocus = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "infbattery";
        c.name = "Infinite Battery";
        c.category = Category::Player;
        c.description = "Phone battery never drains — all hack slots always full.";
        c.failureId = "hook_battery";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::infiniteBattery = on; };
        c.onTick = [&]() {
            if (!g_ptrs.pBattery) return;
            uptr rcx = Memory::Read<uptr>(g_ptrs.pBattery);
            if (!rcx) return;
            for (int s = 0; s < 4; ++s)
                SafeWriteFloat(rcx + 0x10C + s * 4, 1.0f);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "infskillpts";
        c.name = "Infinite Skill Points";
        c.category = Category::Player;
        c.description = "Skill points are never decremented when you buy upgrades.";
        c.safe = false;
        c.onTick = [&]() {
            uptr base = GetPlayerStatsBase();
            if (!base) return;
            i32 sp = SafeReadInt(base + 0xA78);
            if (sp < 99) SafeWriteInt(base + 0xA78, 9999);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "infmoney";
        c.name = "Infinite Money";
        c.category = Category::Player;
        c.description = "Money is always set to a large value.";
        c.safe = false;
        c.onTick = [&]() {
            uptr base = GetPlayerStatsBase();
            if (!base) return;
            i32 money = SafeReadInt(base + 0xA74);
            if (money < 100000) SafeWriteInt(base + 0xA74, 9999999);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "infxp";
        c.name = "Infinite Experience";
        c.category = Category::Player;
        c.description = "Experience points never decrease.";
        c.safe = false;
        c.onTick = [&]() {
            uptr base = GetPlayerStatsBase();
            if (!base) return;
            i32 xp = SafeReadInt(base + 0xA80);
            if (xp < 10000) SafeWriteInt(base + 0xA80, 999999);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "notoriety";
        c.name = "Zero Notoriety";
        c.category = Category::Player;
        c.description = "Notoriety stays at 0 — the public won't recognize you.";
        c.safe = false;
        c.conflicts = { "lockrep" };
        c.onTick = [&]() {
            uptr base = GetPlayerStatsBase();
            if (!base) return;
            SafeWriteInt(base + 0xA74 + 0xD4, 0);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "lockrep";
        c.name = "Lock Reputation";
        c.category = Category::Player;
        c.description = "Reputation value is locked at its current level.";
        c.safe = false;
        c.conflicts = { "notoriety" };
        c.onToggle = [](bool on) { HookFlags::lockReputation = on; };
        add(std::move(c));
    }

    // ===================================================================
    // WEAPONS
    // ===================================================================
    {
        Cheat c;
        c.id = "lockammo";
        c.name = "Infinite Ammo";
        c.category = Category::Weapons;
        c.description = "Ammo count never decreases. Magazine stays full.";
        c.failureId = "hook_ammo";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::lockAmmo = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "lockcraft";
        c.name = "Infinite Craft Materials";
        c.category = Category::Weapons;
        c.description = "Crafting materials are never consumed.";
        c.failureId = "hook_crafting";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::lockCraftMaterials = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "refillwheel";
        c.name = "Refill Item Wheel";
        c.category = Category::Weapons;
        c.description = "Constantly tops up all inventory items on the weapon wheel.";
        c.safe = false;
        c.onTick = [&]() {
            if (!g_ptrs.pAmmo) return;
            uptr rdi = Memory::Read<uptr>(g_ptrs.pAmmo);
            if (!rdi) return;
            for (int i = 0; i < 16; ++i)
            {
                uptr slot = rdi + 0x98 + i * 0x14;
                i32 qty = SafeReadInt(slot);
                if (qty >= 0 && qty < HookFlags::maxCraftQty.load())
                    SafeWriteInt(slot, HookFlags::maxCraftQty.load());
            }
        };
        add(std::move(c));
    }

    // ===================================================================
    // VEHICLES
    // ===================================================================
    {
        Cheat c;
        c.id = "onehitcar";
        c.name = "One-Hit Vehicle Destroy";
        c.category = Category::Vehicles;
        c.description = "All vehicles are instantly destroyed on any hit.";
        c.failureId = "hook_carhealth";
        c.available = !hasFailure(c.failureId);
        c.safe = false;
        c.onToggle = [](bool on) { HookFlags::oneHitCar = on; };
        add(std::move(c));
    }

    // ===================================================================
    // WORLD
    // ===================================================================
    {
        Cheat c;
        c.id = "clearheat";
        c.name = "Clear Wanted Level";
        c.category = Category::World;
        c.description = "Police heat level is zeroed — you are never wanted.";
        c.failureId = "hook_heat";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) {
            HookFlags::clearHeat = on;
            HookFlags::clearPoliceRadar = on;
        };
        c.onTick = [&]() {
            if (!g_ptrs.pHeatLevel) return;
            uptr rsi = Memory::Read<uptr>(g_ptrs.pHeatLevel);
            if (!rsi) return;
            SafeWriteInt(rsi + 0x0C, 0);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "stealth";
        c.name = "Invisible / Undetectable";
        c.category = Category::World;
        c.description = "NPCs and police cannot detect or see you.";
        c.failureId = "hook_stealth";
        c.available = !hasFailure(c.failureId);
        c.safe = false;
        c.onToggle = [](bool on) { HookFlags::stealth = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "infhacktime";
        c.name = "Infinite Hacking Time";
        c.category = Category::World;
        c.description = "Hacking timers never run out.";
        c.failureId = "hook_hacktime";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::infiniteHackTime = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "locktime";
        c.name = "Lock Time of Day";
        c.category = Category::World;
        c.description = "Freezes the in-game clock at the current hour.";
        c.failureId = "hook_timeofday";
        c.available = !hasFailure(c.failureId);
        c.safe = false;
        c.onToggle = [](bool on) { HookFlags::lockTime = on; };
        c.onTick = [&]() {
            if (!HookFlags::lockTime || !g_ptrs.pTime) return;
            uptr rsi = Memory::Read<uptr>(g_ptrs.pTime);
            if (!rsi) return;
            Memory::SafeWrite<u8>(rsi + 0x270, 1);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "settime";
        c.name = "Set Time of Day";
        c.category = Category::World;
        c.description = "Sets the game clock to the configured hour (0-23).";
        c.available = !hasFailure("hook_timeofday");
        c.momentary = true;
        c.safe = false;
        c.onToggle = [](bool on) {
            if (!on || !g_ptrs.pTime) return;
            uptr rsi = Memory::Read<uptr>(g_ptrs.pTime);
            if (!rsi) return;
            float seconds = HookFlags::setTimeHours.load() * 3600.0f;
            Memory::SafeWrite<float>(rsi + 0x758, seconds);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "overidefov";
        c.name = "Override FOV";
        c.category = Category::World;
        c.description = "Sets the camera field of view to a custom value (default 75).";
        c.failureId = "hook_fov";
        c.available = !hasFailure(c.failureId);
        c.safe = false;
        c.onToggle = [](bool on) { HookFlags::overrideFOV = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "ctosstop";
        c.name = "Stop ctOS Box Timer";
        c.category = Category::World;
        c.description = "Freezes the ctOS breach box timer so it never expires.";
        c.failureId = "hook_ctosbox";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::lockCTOSTimer = on; };
        add(std::move(c));
    }

    // ===================================================================
    // MOVEMENT
    // ===================================================================
    {
        Cheat c;
        c.id = "noclip";
        c.name = "No-Clip / Free Roam";
        c.category = Category::Movement;
        c.description = "Enables free-flight movement. WASD+mouse to fly.";
        c.failureId = "hook_coord";
        c.available = !hasFailure(c.failureId);
        c.safe = false;
        c.onToggle = [](bool on) {
            if (on) NoclipEnable();
            else NoclipDisable();
        };
        c.onTick = []() { NoclipTick(); };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "savecords";
        c.name = "Save Coordinates";
        c.category = Category::Movement;
        c.description = "Saves current player position.";
        c.failureId = "hook_coord";
        c.available = !hasFailure(c.failureId);
        c.momentary = true;
        c.safe = false;
        c.onToggle = [](bool on) {
            if (!on || !g_ptrs.pCoord) return;
            uptr coordPtr = Memory::Read<uptr>(g_ptrs.pCoord);
            if (!coordPtr) return;
            float x, y, z;
            Memory::SafeRead<float>(coordPtr + 0x0, x);
            Memory::SafeRead<float>(coordPtr + 0x4, y);
            Memory::SafeRead<float>(coordPtr + 0x8, z);
            extern float g_savedX, g_savedY, g_savedZ;
            g_savedX = x; g_savedY = y; g_savedZ = z;
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "restorecords";
        c.name = "Restore Coordinates";
        c.category = Category::Movement;
        c.description = "Teleports player back to last saved position.";
        c.failureId = "hook_coord";
        c.available = !hasFailure(c.failureId);
        c.momentary = true;
        c.safe = false;
        c.onToggle = [](bool on) {
            if (!on || !g_ptrs.pCoord) return;
            uptr coordPtr = Memory::Read<uptr>(g_ptrs.pCoord);
            if (!coordPtr) return;
            extern float g_savedX, g_savedY, g_savedZ;
            Memory::SafeWrite<float>(coordPtr + 0x0, g_savedX);
            Memory::SafeWrite<float>(coordPtr + 0x4, g_savedY);
            Memory::SafeWrite<float>(coordPtr + 0x8, g_savedZ);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "oneteleport";
        c.name = "Teleport to Waypoint";
        c.category = Category::Movement;
        c.description = "Instantly teleports the player to the set map waypoint.";
        c.failureId = "hook_waypoint";
        c.available = !hasFailure(c.failureId) && g_ptrs.pMapWaypt != 0;
        c.momentary = true;
        c.safe = false;
        c.onToggle = [](bool on) {
            if (!on) return;
            DoWaypointTeleport();
        };
        add(std::move(c));
    }

    // ===================================================================
    // DIGITAL TRIPS
    // ===================================================================
    {
        Cheat c;
        c.id = "spidertimer";
        c.name = "Spider Tank Freeze Timer";
        c.category = Category::DigitalTrip;
        c.description = "Keeps the Spider Tank alive time at maximum (9 minutes).";
        c.failureId = "hook_spidertimer";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::spiderTankFreeze = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "spiderenergy";
        c.name = "Spider Tank Infinite Energy";
        c.category = Category::DigitalTrip;
        c.description = "Spider Tank energy bar stays full.";
        c.safe = false;
        c.onTick = [&]() {
            if (!g_ptrs.pSpiderEnergy) return;
            uptr rbx = Memory::Read<uptr>(g_ptrs.pSpiderEnergy);
            if (!rbx) return;
            SafeWriteFloat(rbx + 0x1008, 1.0f);
        };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "nvznlock";
        c.name = "NVZN Freeze Timer";
        c.category = Category::DigitalTrip;
        c.description = "Freezes the NVZN digital trip countdown timer.";
        c.failureId = "hook_nvzn";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::lockNVZN = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "cashrunlock";
        c.name = "Cash Run Freeze Timer";
        c.category = Category::DigitalTrip;
        c.description = "Prevents the Cash Run timer from incrementing.";
        c.failureId = "hook_cashrun";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::lockCashRunTimer = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "infskilltrip";
        c.name = "Infinite Skill Points (Digital Trips)";
        c.category = Category::DigitalTrip;
        c.description = "Skill points for all Digital Trip modes never decrease.";
        c.safe = true;
        c.onTick = [&]() {
            uptr base = GetPlayerStatsBase();
            if (!base) return;
            if (SafeReadInt(base + 0xA88) < 99) SafeWriteInt(base + 0xA88, 9999);
            if (SafeReadInt(base + 0xA90) < 99) SafeWriteInt(base + 0xA90, 9999);
            if (SafeReadInt(base + 0xA98) < 99) SafeWriteInt(base + 0xA98, 9999);
        };
        add(std::move(c));
    }

    // ===================================================================
    // TIMERS
    // ===================================================================
    {
        Cheat c;
        c.id = "timerfixer";
        c.name = "Timer Fixer";
        c.category = Category::Timers;
        c.description = "Fixes stuck/buggy mission timers.";
        c.failureId = "hook_timerfix";
        c.available = !hasFailure(c.failureId);
        c.safe = true;
        c.onToggle = [](bool on) { HookFlags::lockTimerFixer = on; };
        add(std::move(c));
    }

    {
        Cheat c;
        c.id = "madnesstimer";
        c.name = "Madness Timer Freeze";
        c.category = Category::Timers;
        c.description = "Locks the Madness digital trip timer at 89 seconds.";
        c.safe = true;
        c.onTick = [&]() {
            // Reinforce the value written by the hook
        };
        add(std::move(c));
    }

    // ===================================================================
    // MISC
    // ===================================================================
    {
        Cheat c;
        c.id = "poker1";
        c.name = "Poker: Max Hand Money (P1)";
        c.category = Category::Misc;
        c.description = "Sets Poker Player 1's hand money to max.";
        c.momentary = true;
        c.safe = false;
        c.onToggle = [](bool on) {
            if (!on || !g_ptrs.pPokerPlayer[0]) return;
            uptr p = Memory::Read<uptr>(g_ptrs.pPokerPlayer[0]);
            if (!p) return;
            uptr slot = Memory::Read<uptr>(p);
            if (slot) SafeWriteInt(slot + 0x4, 99999);
        };
        add(std::move(c));
    }

    LOG("Registered %zu cheats", m_cheats.size());
}
