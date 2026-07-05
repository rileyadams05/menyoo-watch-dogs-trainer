#pragma once
#include "../common.h"
#include "memory.h"
#include "pattern_scan.h"

// -----------------------------------------------------------------------
// GamePointers — resolved once after game attach, refreshed as needed.
// These correspond to the globalalloc/registersymbol symbols in Paul44's CT.
// -----------------------------------------------------------------------
struct GamePointers
{
    // ---- Health / God Mode ----
    uptr pChainHealth   = 0;  // Lua-resolved static chain entry
    uptr pHealth        = 0;  // final player health float ptr

    // ---- Experience / XP / Money / Skills ----
    uptr pChainExp      = 0;  // Lua-resolved static chain entry
    // Resolved: pChainExp -> +0x30 -> +0x450 -> +0x18 -> +0x2B0 -> +0x0
    uptr pPlayerStats   = 0;  // points to struct with money/xp/skills

    // ---- Reputation ----
    uptr pChainRep      = 0;
    uptr pReputation    = 0;

    // ---- Focus ----
    uptr pFocus         = 0;  // captured by Focus hook (rcx)

    // ---- Battery ----
    uptr pBattery       = 0;  // captured by Battery hook (rcx)

    // ---- Coordinates ----
    uptr pCoord         = 0;  // captured by CoordWrite hook (rax)
    uptr pYawPitch      = 0;  // captured by YawPitch hook (rdi)

    // ---- Heat / Police ----
    uptr pHeatLevel     = 0;  // captured by HeatLevel hook

    // ---- Ammo ----
    uptr pAmmo          = 0;  // captured by LockAmmo hook (rdi)

    // ---- Car ----
    uptr pChainCar      = 0;
    uptr pCar           = 0;

    // ---- Time ----
    uptr pTime          = 0;  // captured by Time hook (rsi)

    // ---- FOV ----
    uptr pFOV           = 0;  // captured by FOV hook (rcx)

    // ---- Map Waypoint ----
    uptr pMapWaypt      = 0;  // static Lua scan

    // ---- Timer structs ----
    uptr pHackTime      = 0;
    uptr pHackTime2     = 0;
    uptr pTimeCTbox     = 0;
    uptr pTimeFixer     = 0;
    uptr pTimerWarn1    = 0;
    uptr pTimerNVZN     = 0;
    uptr pTimeCashrun   = 0;
    uptr pTimerSpider   = 0;
    uptr pSpiderEnergy  = 0;
    uptr pSpiderCannon  = 0;

    // ---- Poker ----
    uptr pPokerPlayer[4] = {};

    // ---- Progression ----
    uptr pChainProgr    = 0;

    // ---- Misc ----
    bool valid          = false;

    void Reset() { *this = GamePointers{}; }
};

extern GamePointers g_ptrs;

// -----------------------------------------------------------------------
// Resolve all static pointer chains using Lua-equivalent AOB scans.
// Called once after Disrupt_b64.dll is loaded.
// -----------------------------------------------------------------------
bool ResolveGamePointers();
