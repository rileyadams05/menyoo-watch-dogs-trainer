#pragma once
#include "../common.h"

// -----------------------------------------------------------------------
// Hook manager — installs / uninstalls all mid-function hooks
// -----------------------------------------------------------------------
namespace Hooks
{
    bool InstallAll();
    void UninstallAll();
    bool IsInitialized();

    struct Failure
    {
        std::string id;
        std::string reason;
    };

    std::vector<Failure> GetFailures();
    bool HasFailure(const std::string& id);
    void ReportFailure(const std::string& id, const std::string& reason);
    void ClearFailure(const std::string& id);

    // Per-subsystem install (called from InstallAll)
    bool InstallGodMode();
    bool InstallFocus();
    bool InstallBattery();
    bool InstallAmmo();
    bool InstallCrafting();
    bool InstallCoordinates();
    bool InstallHeatLevel();
    bool InstallHackTime();
    bool InstallCarHealth();
    bool InstallTimeOfDay();
    bool InstallFOV();
    bool InstallReputation();
    bool InstallSpiderTank();
    bool InstallNVZN();
    bool InstallCashRun();
    bool InstallTimers();
    bool InstallStealth();
}

// -----------------------------------------------------------------------
// Hook state flags — read by cheat modules to enable / disable effects
// -----------------------------------------------------------------------
namespace HookFlags
{
    extern std::atomic<bool> godMode;
    extern std::atomic<bool> infiniteFocus;
    extern std::atomic<bool> infiniteBattery;
    extern std::atomic<bool> lockAmmo;
    extern std::atomic<bool> lockCraftMaterials;
    extern std::atomic<bool> infiniteSkillPoints;
    extern std::atomic<bool> lockReputation;
    extern std::atomic<bool> clearHeat;
    extern std::atomic<bool> clearPoliceRadar;
    extern std::atomic<bool> stealth;
    extern std::atomic<bool> infiniteHackTime;
    extern std::atomic<bool> oneHitCar;
    extern std::atomic<bool> lockTime;
    extern std::atomic<bool> overrideFOV;
    extern std::atomic<bool> spiderTankFreeze;
    extern std::atomic<bool> lockNVZN;
    extern std::atomic<bool> lockCashRunTimer;
    extern std::atomic<bool> lockCTOSTimer;
    extern std::atomic<bool> lockTimerFixer;

    // Write-once values set from UI
    extern std::atomic<float> focusMax;
    extern std::atomic<float> batteryMax;
    extern std::atomic<float> fovValue;
    extern std::atomic<float> dropHeight;
    extern std::atomic<i32>   maxAmmo;
    extern std::atomic<i32>   maxCraftQty;
    extern std::atomic<float> setTimeHours;
    extern std::atomic<i32>   wantedLevel;   // 0 = clear
}
