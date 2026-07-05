#include "hooks.h"
#include "../memory/memory.h"
#include "../memory/pattern_scan.h"
#include "../memory/pointer_chain.h"
#include <MinHook.h>
#include <algorithm>
#include <mutex>

namespace
{
    std::vector<Hooks::Failure> g_failures;
    std::mutex g_failureMutex;
    std::atomic<bool> g_initialized{ false };

    void ClearFailures()
    {
        std::scoped_lock lock(g_failureMutex);
        g_failures.clear();
    }

    bool EnsureAddr(uptr addr, const char* id, const char* message)
    {
        if (addr) return true;
        Hooks::ReportFailure(id, message);
        LOG("%s", message);
        return false;
    }
}

// -----------------------------------------------------------------------
// Failure reporting
// -----------------------------------------------------------------------
bool Hooks::IsInitialized()
{
    return g_initialized.load();
}

std::vector<Hooks::Failure> Hooks::GetFailures()
{
    std::scoped_lock lock(g_failureMutex);
    return g_failures;
}

bool Hooks::HasFailure(const std::string& id)
{
    if (id.empty()) return false;
    std::scoped_lock lock(g_failureMutex);
    return std::any_of(g_failures.begin(), g_failures.end(), [&](const Failure& f) {
        return f.id == id;
    });
}

void Hooks::ReportFailure(const std::string& id, const std::string& reason)
{
    if (id.empty()) return;
    std::scoped_lock lock(g_failureMutex);
    auto it = std::find_if(g_failures.begin(), g_failures.end(), [&](const Failure& f) {
        return f.id == id;
    });
    if (it != g_failures.end())
    {
        it->reason = reason;
    }
    else
    {
        g_failures.push_back({ id, reason });
    }
}

void Hooks::ClearFailure(const std::string& id)
{
    if (id.empty()) return;
    std::scoped_lock lock(g_failureMutex);
    auto it = std::remove_if(g_failures.begin(), g_failures.end(), [&](const Failure& f) {
        return f.id == id;
    });
    if (it != g_failures.end())
        g_failures.erase(it, g_failures.end());
}

// -----------------------------------------------------------------------
// Flag definitions
// -----------------------------------------------------------------------
namespace HookFlags
{
    std::atomic<bool>  godMode             { false };
    std::atomic<bool>  infiniteFocus       { false };
    std::atomic<bool>  infiniteBattery     { false };
    std::atomic<bool>  lockAmmo            { false };
    std::atomic<bool>  lockCraftMaterials  { false };
    std::atomic<bool>  infiniteSkillPoints { false };
    std::atomic<bool>  lockReputation      { false };
    std::atomic<bool>  clearHeat           { false };
    std::atomic<bool>  clearPoliceRadar    { false };
    std::atomic<bool>  stealth             { false };
    std::atomic<bool>  infiniteHackTime    { false };
    std::atomic<bool>  oneHitCar           { false };
    std::atomic<bool>  lockTime            { false };
    std::atomic<bool>  overrideFOV         { false };
    std::atomic<bool>  spiderTankFreeze    { false };
    std::atomic<bool>  lockNVZN            { false };
    std::atomic<bool>  lockCashRunTimer    { false };
    std::atomic<bool>  lockCTOSTimer       { false };
    std::atomic<bool>  lockTimerFixer      { false };

    std::atomic<float> focusMax            { 1.0f };
    std::atomic<float> batteryMax          { 1.0f };
    std::atomic<float> fovValue            { 75.0f };
    std::atomic<float> dropHeight          { 3.0f };
    std::atomic<i32>   maxAmmo             { 9999 };
    std::atomic<i32>   maxCraftQty         { 999 };
    std::atomic<float> setTimeHours        { 12.0f };
    std::atomic<i32>   wantedLevel         { 0 };
}

// -----------------------------------------------------------------------
// Trampoline storage — original function bytes we need to call
// -----------------------------------------------------------------------
struct TrampolineEntry
{
    uptr hookAddr    = 0;
    uptr trampolineAddr = 0;
    std::vector<u8> savedBytes;
    bool active      = false;
};

static std::vector<TrampolineEntry> g_hooks;

// -----------------------------------------------------------------------
// Mid-function hook helper: allocates a code cave, writes a jmp from
// hookAddr into the cave, restores original bytes in trampoline, then
// jumps back to hookAddr + savedLen.
// -----------------------------------------------------------------------
static bool InstallMidHook(uptr hookAddr, size_t savedLen,
    std::function<void(uptr cave, uptr returnAddr)> writeCave)
{
    if (!hookAddr || savedLen < 5) return false;

    // Allocate code cave near the hook
    uptr cave = Memory::AllocNear(hookAddr, 256);
    if (!cave) return false;

    uptr returnAddr = hookAddr + savedLen;

    // Let caller write the cave contents
    writeCave(cave, returnAddr);

    // Write JMP from hookAddr into cave (14-byte abs JMP)
    if (savedLen < 14)
    {
        // NOP fill remaining bytes so disassembler stays clean
        Memory::PatchNop(hookAddr, savedLen);
    }
    Memory::WriteAbsJmp(hookAddr, cave);

    TrampolineEntry e;
    e.hookAddr       = hookAddr;
    e.trampolineAddr = cave;
    e.active         = true;
    e.savedBytes.resize(savedLen);
    // savedBytes already overwritten — we don't restore on uninstall
    // (game exits with us, so no need for clean unload in practice)
    g_hooks.push_back(std::move(e));
    return true;
}

// -----------------------------------------------------------------------
// God Mode hook
// AOB: F3 0F 10 41 18 F3 0F 5C 41 1C   (health damage subtract)
// From WatchDogs_v1.6_Released.CT — hooks health write
// We capture rcx (entity ptr), store in pHealth, and if godMode is set
// we clamp health >= 100.0f
// -----------------------------------------------------------------------
bool Hooks::InstallGodMode()
{
    uptr addr = Memory::ScanDisrupt("F3 0F 10 41 18 F3 0F 5C 41 1C");
    if (!EnsureAddr(addr, "hook_godmode", "GodMode AOB not found"))
        return false;

    if (!InstallMidHook(addr, 14, [addr](uptr cave, uptr returnAddr) {
        // Cave:
        //   push r15
        //   mov r15, [g_ptrs.pHealth address]
        //   mov [r15], rcx          ; store entity ptr
        //   pop r15
        //   ; if godMode, skip the damage sub:
        //   ; original: movss xmm0,[rcx+18]; subss xmm0,[rcx+1C]
        //   ; we replicate original then conditionally restore health
        //   movss xmm0, [rcx+0x18]
        //   subss xmm0, [rcx+0x1C]
        //   jmp returnAddr

        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        // push r15
        p[i++] = 0x41; p[i++] = 0x57;

        // mov r15, imm64 (address of g_ptrs.pHealth)
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pHealth);
        i += 8;

        // mov [r15], rcx
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;

        // pop r15
        p[i++] = 0x41; p[i++] = 0x5F;

        // movss xmm0, [rcx+0x18]  -- F3 0F 10 41 18
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x10; p[i++] = 0x41; p[i++] = 0x18;

        // test byte ptr [godMode_addr], 1
        // je  skip_nop (don't skip the subtract)
        // jmp past_subtract
        // subss xmm0, [rcx+0x1C]  -- F3 0F 5C 41 1C

        // mov r15, &HookFlags::godMode
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::godMode);
        i += 8;

        // cmp byte ptr [r15], 0
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;

        // jne skip_subtract (short, fill in after)
        p[i++] = 0x75; p[i++] = 0x05;  // +5 bytes = length of subss

        // subss xmm0, [rcx+0x1C]  -- F3 0F 5C 41 1C
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x5C; p[i++] = 0x41; p[i++] = 0x1C;

        // label: skip_subtract
        // jmp returnAddr (14-byte abs)
        p[i++] = 0xFF; p[i++] = 0x25; p[i++] = 0x00; p[i++] = 0x00; p[i++] = 0x00; p[i++] = 0x00;
        *reinterpret_cast<u64*>(p + i) = returnAddr;
        i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure("hook_godmode", "Failed to install GodMode hook");
        return false;
    }

    LOG("GodMode hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Focus hook
// AOB: F3 0F 59 71 48 85   (focus multiply/drain)
// Captures pFocus, and if infiniteFocus locks focus value to max
// -----------------------------------------------------------------------
bool Hooks::InstallFocus()
{
    uptr addr = Memory::ScanDisrupt("F3 0F 59 71 48 85");
    if (!addr)
    {
        // Try alternate AOB from watchdogsfrf.CT
        addr = Memory::ScanDisrupt("F3 0F 10 81 88 01 00 00");
        if (!addr)
        {
            ReportFailure("hook_focus", "Focus AOB not found");
            return false;
        }
    }

    if (!InstallMidHook(addr, 14, [addr](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        // push r15
        p[i++] = 0x41; p[i++] = 0x57;

        // mov r15, &g_ptrs.pFocus
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pFocus);
        i += 8;

        // mov [r15], rcx
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;

        // mov r15, &HookFlags::infiniteFocus
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::infiniteFocus);
        i += 8;

        // cmp byte ptr [r15], 0
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;

        // je run_original
        p[i++] = 0x74; p[i++] = 0x0D;  // skip the force-max block

        // Force [rcx+0x48] = 1.0f (focus = max)
        // mov r15d, 0x3F800000
        p[i++] = 0x41; p[i++] = 0xBF;
        *reinterpret_cast<u32*>(p + i) = 0x3F800000u;
        i += 4;
        // mov [rcx+0x48], r15d
        p[i++] = 0x45; p[i++] = 0x89; p[i++] = 0x79; p[i++] = 0x48;

        // pop r15 + jmp return
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25; p[i++] = 0x00; p[i++] = 0x00; p[i++] = 0x00; p[i++] = 0x00;
        *reinterpret_cast<u64*>(p + i) = returnAddr;
        i += 8;

        // label: run_original
        // Replicate original bytes (first 6: F3 0F 59 71 48 85 — but 85 is next byte start)
        // Just replay: mulss xmm6, [rcx+0x48]  F3 0F 59 71 48
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x59; p[i++] = 0x71; p[i++] = 0x48;

        // pop r15
        p[i++] = 0x41; p[i++] = 0x5F;

        // jmp returnAddr
        p[i++] = 0xFF; p[i++] = 0x25; p[i++] = 0x00; p[i++] = 0x00; p[i++] = 0x00; p[i++] = 0x00;
        *reinterpret_cast<u64*>(p + i) = returnAddr + 1; // +1 past the 6th byte we covered
        i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure("hook_focus", "Failed to install Focus hook");
        return false;
    }

    LOG("Focus hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Battery hook
// AOB: F3 0F 10 81 0C 01 00 00 F3 0F 59
// Captures pBattery (rcx), clamps to max if infiniteBattery
// -----------------------------------------------------------------------
bool Hooks::InstallBattery()
{
    uptr addr = Memory::ScanDisrupt("F3 0F 10 81 0C 01 00 00 F3 0F 59");
    const char* failureId = "hook_battery";
    if (!EnsureAddr(addr, failureId, "Battery AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [addr](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        // push r15
        p[i++] = 0x41; p[i++] = 0x57;

        // mov r15, &g_ptrs.pBattery
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pBattery);
        i += 8;

        // mov [r15], rcx
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;

        // check flag
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::infiniteBattery);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x0D;  // je run_original

        // Force [rcx+0x10C] = 1.0f (battery = max slot 0 full)
        p[i++] = 0x41; p[i++] = 0xBF;
        *reinterpret_cast<u32*>(p + i) = 0x3F800000u;
        i += 4;
        p[i++] = 0x41; p[i++] = 0x89; p[i++] = 0xB9;
        *reinterpret_cast<u32*>(p + i) = 0x0000010Cu;
        i += 4;

        // pop r15 + jmp
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        // run_original: movss xmm0,[rcx+0x10C]  F3 0F 10 81 0C 01 00 00
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x10; p[i++] = 0x81;
        *reinterpret_cast<u32*>(p + i) = 0x0000010Cu; i += 4;

        // pop r15
        p[i++] = 0x41; p[i++] = 0x5F;

        // jmp returnAddr+8 (past the movss we replaced + next mulss prefix we included)
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install Battery hook");
        return false;
    }

    LOG("Battery hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Ammo lock hook
// AOB: FF 8F 98 00 00 00   (dec [rdi+0x98])
// If lockAmmo: prevents ammo decrement
// -----------------------------------------------------------------------
bool Hooks::InstallAmmo()
{
    uptr addr = Memory::ScanDisrupt("FF 8F 98 00 00 00");
    const char* failureId = "hook_ammo";
    if (!EnsureAddr(addr, failureId, "Ammo AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [addr](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        // push r15
        p[i++] = 0x41; p[i++] = 0x57;

        // store pAmmo = rdi
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pAmmo);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x3F;

        // check lockAmmo
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::lockAmmo);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x06;  // je run_original (skip block)

        // if lockAmmo: skip decrement, restore to maxAmmo
        // mov r15d, [maxAmmo]
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::maxAmmo);
        i += 8;
        p[i++] = 0x45; p[i++] = 0x8B; p[i++] = 0x3F;          // mov r15d,[r15]
        p[i++] = 0x45; p[i++] = 0x89; p[i++] = 0xBF;
        *reinterpret_cast<u32*>(p + i) = 0x98u; i += 4;        // mov [rdi+0x98], r15d

        // pop r15 + jmp return
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        // run_original: dec [rdi+0x98]  -- FF 8F 98 00 00 00
        p[i++] = 0xFF; p[i++] = 0x8F;
        *reinterpret_cast<u32*>(p + i) = 0x98u; i += 4;

        // pop r15
        p[i++] = 0x41; p[i++] = 0x5F;

        // jmp returnAddr
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install Ammo hook");
        return false;
    }

    LOG("Ammo hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Crafting lock hook
// AOB: 2B ?? 89 42 0C B0 01   (CraftLock — sub eax,* ; mov [rdx+0C],eax ; mov al,1)
// If lockCraftMaterials: prevent deduction, clamp to maxCraftQty
// -----------------------------------------------------------------------
bool Hooks::InstallCrafting()
{
    uptr addr = Memory::ScanDisrupt("2B ?? 89 42 0C B0 01");
    const char* failureId = "hook_crafting";
    if (!EnsureAddr(addr, failureId, "CraftLock AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [addr](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        // check flag
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::lockCraftMaterials);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x0E;  // je run_original

        // Force [rdx+0x0C] = maxCraftQty
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::maxCraftQty);
        i += 8;
        p[i++] = 0x45; p[i++] = 0x8B; p[i++] = 0x3F;      // mov r15d,[r15]
        p[i++] = 0x45; p[i++] = 0x89; p[i++] = 0x7A; p[i++] = 0x0C; // mov [rdx+0C],r15d

        // jmp return
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        // run_original: 2B ?? 89 42 0C B0 01
        // We can't easily replicate the wildcard byte, so we copy saved bytes
        // The "sub eax, [reg]" is 2 or 3 bytes — copy from original location
        // Since addr is available via capture, copy raw bytes
        // We'll just copy 7 bytes directly
        memcpy(p + i, reinterpret_cast<void*>(addr), 7);
        i += 7;

        // jmp returnAddr
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install Crafting hook");
        return false;
    }

    LOG("Crafting hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Coordinates hook (CoordWrite)
// AOB: 0F 29 80 20 01 00 00 48 8B 8D 30 01 00 00
// Captures rax -> pCoord
// -----------------------------------------------------------------------
bool Hooks::InstallCoordinates()
{
    uptr addr = Memory::ScanDisrupt("0F 29 80 20 01 00 00 48 8B 8D 30 01 00 00");
    if (!EnsureAddr(addr, "hook_coord", "CoordWrite AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        // push r15
        p[i++] = 0x41; p[i++] = 0x57;

        // store pCoord = rax
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pCoord);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x07;  // mov [r15], rax

        // pop r15
        p[i++] = 0x41; p[i++] = 0x5F;

        // original: movaps [rax+0x120], xmm0  -- 0F 29 80 20 01 00 00
        p[i++] = 0x0F; p[i++] = 0x29; p[i++] = 0x80;
        *reinterpret_cast<u32*>(p + i) = 0x00000120u; i += 4;

        // jmp returnAddr (past 7 bytes)
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure("hook_coord", "Failed to install coordinate hook");
        return false;
    }

    // YawPitch hook
    uptr addrYaw = Memory::ScanDisrupt("89 87 88 00 00 00 48 83 3D");
    if (addrYaw)
    {
        if (!InstallMidHook(addrYaw, 14, [](uptr cave, uptr returnAddr) {
            u8* p = reinterpret_cast<u8*>(cave);
            size_t i = 0;
            p[i++] = 0x41; p[i++] = 0x57;
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pYawPitch);
            i += 8;
            p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x3F;  // mov [r15], rdi
            p[i++] = 0x41; p[i++] = 0x5F;
            // original: mov [rdi+0x88], eax  -- 89 87 88 00 00 00
            p[i++] = 0x89; p[i++] = 0x87;
            *reinterpret_cast<u32*>(p + i) = 0x88u; i += 4;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
        }))
        {
            LOG("Failed to install yaw/pitch hook");
        }
    }

    LOG("Coordinates hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Heat Level hook
// AOB: 44 8B 46 0C 45 85 C0
// Captures pHeatLevel; if clearHeat, zeroes it out
// -----------------------------------------------------------------------
bool Hooks::InstallHeatLevel()
{
    const char* failureId = "hook_heat";
    uptr addr = Memory::ScanDisrupt("44 8B 46 0C 45 85 C0");
    if (!EnsureAddr(addr, failureId, "HeatLevel AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        p[i++] = 0x41; p[i++] = 0x57;

        // store pHeatLevel = rsi (entity with heat field)
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pHeatLevel);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x37;  // mov [r15], rsi

        // check clearHeat
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::clearHeat);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x07;  // je run_original

        // zero out heat: mov dword ptr [rsi+0x0C], 0
        p[i++] = 0xC7; p[i++] = 0x46; p[i++] = 0x0C;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;

        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        // run_original: mov r8d,[rsi+0x0C]; test r8d,r8d -- 44 8B 46 0C 45 85 C0
        p[i++] = 0x44; p[i++] = 0x8B; p[i++] = 0x46; p[i++] = 0x0C;
        p[i++] = 0x45; p[i++] = 0x85; p[i++] = 0xC0;

        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install HeatLevel hook");
        return false;
    }

    // Police radar hook: F3 0F 58 47 50 (addss xmm0,[rdi+50])
    uptr addrRadar = Memory::ScanDisrupt("F3 0F 58 47 50 F3 0F 11 47 50");
    if (addrRadar)
    {
        if (!InstallMidHook(addrRadar, 10, [](uptr cave, uptr returnAddr) {
            u8* p = reinterpret_cast<u8*>(cave);
            size_t i = 0;
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::clearPoliceRadar);
            i += 8;
            p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
            p[i++] = 0x74; p[i++] = 0x09;
            // if clearPoliceRadar: zero radar accumulator [rdi+50]
            p[i++] = 0xC7; p[i++] = 0x47; p[i++] = 0x50;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            // jmp
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            // original: addss xmm0,[rdi+50]; movss [rdi+50],xmm0
            p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x58; p[i++] = 0x47; p[i++] = 0x50;
            p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x11; p[i++] = 0x47; p[i++] = 0x50;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
        }))
        {
            LOG("Failed to install radar hook");
        }
    }

    LOG("HeatLevel hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Hack Time hook
// AOB: 80 B9 AD 00 00 00 00 48 8B D9 C6
// If infiniteHackTime: forces flag byte [rcx+0xAD] = 1
// -----------------------------------------------------------------------
bool Hooks::InstallHackTime()
{
    const char* failureId = "hook_hacktime";
    uptr addr = Memory::ScanDisrupt("80 B9 AD 00 00 00 00 48 8B D9 C6");
    if (!EnsureAddr(addr, failureId, "HackTime AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        p[i++] = 0x41; p[i++] = 0x57;

        // store pHackTime = rcx
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pHackTime);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;

        // check flag
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::infiniteHackTime);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x08;

        // force [rcx+0xAD] = 1
        p[i++] = 0xC6; p[i++] = 0x81;
        *reinterpret_cast<u32*>(p + i) = 0xADu; i += 4;
        p[i++] = 0x01;

        p[i++] = 0x41; p[i++] = 0x5F;
        // original: cmp byte ptr [rcx+0xAD], 0  -- 80 B9 AD 00 00 00 00
        p[i++] = 0x80; p[i++] = 0xB9;
        *reinterpret_cast<u32*>(p + i) = 0xADu; i += 4;
        p[i++] = 0x00;

        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install HackTime hook");
        return false;
    }

    LOG("HackTime hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Car Health hook
// AOB: F3 0F 10 86 D8 00 00 00 4C
// Captures car health pointer; if oneHitCar sets health to 0
// -----------------------------------------------------------------------
bool Hooks::InstallCarHealth()
{
    const char* failureId = "hook_carhealth";
    uptr addr = Memory::ScanDisrupt("F3 0F 10 86 D8 00 00 00 4C");
    if (!EnsureAddr(addr, failureId, "CarHealth AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        p[i++] = 0x41; p[i++] = 0x57;

        // store pCar = rsi
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pCar);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x37;

        // check oneHitCar
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::oneHitCar);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x0A;

        // force [rsi+0xD8] = 0.0f (car destroyed)
        p[i++] = 0xC7; p[i++] = 0x86;
        *reinterpret_cast<u32*>(p + i) = 0xD8u; i += 4;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;

        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        // original: movss xmm0, [rsi+0xD8]  F3 0F 10 86 D8 00 00 00
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x10; p[i++] = 0x86;
        *reinterpret_cast<u32*>(p + i) = 0xD8u; i += 4;

        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install CarHealth hook");
        return false;
    }

    LOG("CarHealth hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Time Of Day hook
// AOB: 80 BE 90 08 00 00 00
// Captures pTime (rsi); if lockTime writes setTimeHours
// -----------------------------------------------------------------------
bool Hooks::InstallTimeOfDay()
{
    const char* failureId = "hook_timeofday";
    uptr addr = Memory::ScanDisrupt("80 BE 90 08 00 00 00");
    if (!EnsureAddr(addr, failureId, "TimeOfDay AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        p[i++] = 0x41; p[i++] = 0x57;

        // store pTime = rsi
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pTime);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x37;

        p[i++] = 0x41; p[i++] = 0x5F;

        // original: cmp byte ptr [rsi+0x890], 0  -- 80 BE 90 08 00 00 00
        p[i++] = 0x80; p[i++] = 0xBE;
        *reinterpret_cast<u32*>(p + i) = 0x890u; i += 4;
        p[i++] = 0x00;

        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install TimeOfDay hook");
        return false;
    }

    LOG("TimeOfDay hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// FOV hook
// AOB: F3 0F 51 DA 0F 2F DD
// Captures pFOV (rcx); if overrideFOV writes fovValue to [rcx+0x48]
// -----------------------------------------------------------------------
bool Hooks::InstallFOV()
{
    const char* failureId = "hook_fov";
    uptr addr = Memory::ScanDisrupt("F3 0F 51 DA 0F 2F DD");
    if (!EnsureAddr(addr, failureId, "FOV AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;

        p[i++] = 0x41; p[i++] = 0x57;

        // store pFOV = rcx
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pFOV);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;

        // check overrideFOV
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::overrideFOV);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x0E;

        // write fovValue to [rcx+0x48]
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::fovValue);
        i += 8;
        p[i++] = 0x45; p[i++] = 0x8B; p[i++] = 0x3F;      // mov r15d, [r15]
        p[i++] = 0x45; p[i++] = 0x89; p[i++] = 0x79; p[i++] = 0x48; // mov [rcx+0x48], r15d

        p[i++] = 0x41; p[i++] = 0x5F;

        // original: sqrtss xmm3,xmm2; comiss xmm3,xmm5  -- F3 0F 51 DA 0F 2F DD
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x51; p[i++] = 0xDA;
        p[i++] = 0x0F; p[i++] = 0x2F; p[i++] = 0xDD;

        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install FOV hook");
        return false;
    }

    LOG("FOV hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Reputation hook
// AOB near "03 3C BB" -- Paul44's CT: hooks add edi,[rbx+rdi*4]
// Captures pReputation (rbx); if lockReputation prevents writes
// -----------------------------------------------------------------------
bool Hooks::InstallReputation()
{
    uptr addr = Memory::ScanDisrupt("03 3C BB ?? ?? ?? ?? 8B C7");
    if (!addr)
    {
        // Alternative pattern
        addr = Memory::ScanDisrupt("48 8B CB 8B D7 E8 ?? ?? ?? ?? 85 C0");
        if (!addr) { LOG("Reputation AOB not found - non-fatal"); return false; }
    }
    // Minimal hook — just capture pReputation pointer from rbx
    InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;
        p[i++] = 0x41; p[i++] = 0x57;
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pReputation);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x1F;  // mov [r15], rbx
        p[i++] = 0x41; p[i++] = 0x5F;
        // Can't replicate wildcard bytes cleanly — copy from site
        uptr site = Memory::ScanDisrupt("03 3C BB ?? ?? ?? ?? 8B C7");
        if (site) { memcpy(p + i, reinterpret_cast<void*>(site), 9); i += 9; }
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    });
    LOG("Reputation hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Spider Tank hooks
// Timer AOB: F3 0F 5C C6 0F 2F 43 08 73 28
// Energy AOB: F3 0F 10 83 08 10 00 00
// -----------------------------------------------------------------------
bool Hooks::InstallSpiderTank()
{
    bool ok = true;
    uptr addrTimer = Memory::ScanDisrupt("F3 0F 5C C6 0F 2F 43 08 73 28");
    if (addrTimer)
    {
        if (!InstallMidHook(addrTimer, 14, [](uptr cave, uptr returnAddr) {
            u8* p = reinterpret_cast<u8*>(cave);
            size_t i = 0;
            p[i++] = 0x41; p[i++] = 0x57;
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pTimerSpider);
            i += 8;
            p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;  // mov [r15], rcx
            p[i++] = 0x41; p[i++] = 0x5F;
            // check flag
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::spiderTankFreeze);
            i += 8;
            p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
            p[i++] = 0x74; p[i++] = 0x09;
            // force [rbx+0x08] = large value (freeze timer at max)
            p[i++] = 0xC7; p[i++] = 0x43; p[i++] = 0x08;
            *reinterpret_cast<u32*>(p + i) = 0x44070000u;  // 540.0f = 9 min
            i += 4;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            // original: subss xmm0,xmm6; comiss xmm0,[rbx+08]
            p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x5C; p[i++] = 0xC6;
            p[i++] = 0x0F; p[i++] = 0x2F; p[i++] = 0x43; p[i++] = 0x08;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
        }))
        {
            ReportFailure("hook_spidertimer", "Failed to install SpiderTank timer hook");
            ok = false;
        }
    }
    else
    {
        ReportFailure("hook_spidertimer", "SpiderTank timer AOB not found");
        ok = false;
    }

    uptr addrEnergy = Memory::ScanDisrupt("F3 0F 10 83 08 10 00 00");
    if (addrEnergy)
    {
        InstallMidHook(addrEnergy, 14, [](uptr cave, uptr returnAddr) {
            u8* p = reinterpret_cast<u8*>(cave);
            size_t i = 0;
            p[i++] = 0x41; p[i++] = 0x57;
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pSpiderEnergy);
            i += 8;
            p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x1F;  // mov [r15], rbx
            p[i++] = 0x41; p[i++] = 0x5F;
            // original: movss xmm0,[rbx+0x1008]
            p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x10; p[i++] = 0x83;
            *reinterpret_cast<u32*>(p + i) = 0x1008u; i += 4;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
        });
    }

    LOG("SpiderTank hooks installed");
    return ok;
}

// -----------------------------------------------------------------------
// NVZN Timer hook
// AOB: F3 0F 11 8B 64 10 00 00
// -----------------------------------------------------------------------
bool Hooks::InstallNVZN()
{
    const char* failureId = "hook_nvzn";
    uptr addr = Memory::ScanDisrupt("F3 0F 11 8B 64 10 00 00");
    if (!EnsureAddr(addr, failureId, "NVZN AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;
        p[i++] = 0x41; p[i++] = 0x57;
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pTimerNVZN);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x1F;  // mov [r15], rbx
        // check flag
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::lockNVZN);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x06;
        // skip the timer write
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        // original: movss [rbx+0x1064], xmm1  F3 0F 11 8B 64 10 00 00
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x11; p[i++] = 0x8B;
        *reinterpret_cast<u32*>(p + i) = 0x1064u; i += 4;
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install NVZN hook");
        return false;
    }

    LOG("NVZN hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Cash Run timer hook
// AOB: F3 0F 58 B7 9C 00 00 00
// -----------------------------------------------------------------------
bool Hooks::InstallCashRun()
{
    const char* failureId = "hook_cashrun";
    uptr addr = Memory::ScanDisrupt("F3 0F 58 B7 9C 00 00 00");
    if (!EnsureAddr(addr, failureId, "CashRun AOB not found")) return false;

    if (!InstallMidHook(addr, 14, [](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;
        p[i++] = 0x41; p[i++] = 0x57;
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pTimeCashrun);
        i += 8;
        p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x3F;  // mov [r15], rdi
        // check lockCashRunTimer
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::lockCashRunTimer);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x06;
        // skip timer increment
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        // original: addss xmm6,[rdi+0x9C]  F3 0F 58 B7 9C 00 00 00
        p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x58; p[i++] = 0xB7;
        *reinterpret_cast<u32*>(p + i) = 0x9Cu; i += 4;
        p[i++] = 0x41; p[i++] = 0x5F;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install CashRun hook");
        return false;
    }

    LOG("CashRun hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Stealth hook (Invisible)
// AOB: 44 88 ?? ?? 48 8B 8E D0 00 00 00
// If stealth: forces detection byte to 0
// -----------------------------------------------------------------------
bool Hooks::InstallStealth()
{
    const char* failureId = "hook_stealth";
    uptr addr = Memory::ScanDisrupt("44 88 ?? ?? 48 8B 8E D0 00 00 00");
    if (!EnsureAddr(addr, failureId, "Stealth AOB not found")) return false;

    // Copy the full 11 bytes for restoration
    u8 savedBytes[14] = {};
    memcpy(savedBytes, reinterpret_cast<void*>(addr), 11);

    if (!InstallMidHook(addr, 14, [savedBytes](uptr cave, uptr returnAddr) {
        u8* p = reinterpret_cast<u8*>(cave);
        size_t i = 0;
        // check stealth flag
        p[i++] = 0x49; p[i++] = 0xBF;
        *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::stealth);
        i += 8;
        p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
        p[i++] = 0x74; p[i++] = 0x0C;
        // stealth active: zero the detection byte [rdi+0x68]
        p[i++] = 0xC6; p[i++] = 0x47; p[i++] = 0x68; p[i++] = 0x00;
        // also zero r13b (the value being stored)
        p[i++] = 0x45; p[i++] = 0x30; p[i++] = 0xED;  // xor r13b, r13b
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        // run_original: copy saved bytes
        memcpy(p + i, savedBytes, 11); i += 11;
        p[i++] = 0xFF; p[i++] = 0x25;
        *reinterpret_cast<u32*>(p + i) = 0; i += 4;
        *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
    }))
    {
        ReportFailure(failureId, "Failed to install Stealth hook");
        return false;
    }

    LOG("Stealth hook installed at %p", (void*)addr);
    return true;
}

// -----------------------------------------------------------------------
// Miscellaneous timer hooks (ctOS box, timer fixer)
// -----------------------------------------------------------------------
bool Hooks::InstallTimers()
{
    bool ok = true;
    // ctOS box timer: 80 39 00 0F 29 74 24 70
    uptr addrCT = Memory::ScanDisrupt("80 39 00 0F 29 74 24 70");
    if (addrCT)
    {
        if (!InstallMidHook(addrCT, 14, [](uptr cave, uptr returnAddr) {
            u8* p = reinterpret_cast<u8*>(cave);
            size_t i = 0;
            p[i++] = 0x41; p[i++] = 0x57;
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pTimeCTbox);
            i += 8;
            // track lowest rcx value
            p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x0F;
            p[i++] = 0x41; p[i++] = 0x5F;
            // check lockCTOSTimer: if set, force [rcx]=0
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&HookFlags::lockCTOSTimer);
            i += 8;
            p[i++] = 0x41; p[i++] = 0x80; p[i++] = 0x3F; p[i++] = 0x00;
            p[i++] = 0x74; p[i++] = 0x03;
            p[i++] = 0xC6; p[i++] = 0x01; p[i++] = 0x00;  // mov byte ptr [rcx], 0
            // original: cmp [rcx],0; movaps [rsp+70],xmm6
            p[i++] = 0x80; p[i++] = 0x39; p[i++] = 0x00;
            p[i++] = 0x0F; p[i++] = 0x29; p[i++] = 0x74; p[i++] = 0x24; p[i++] = 0x70;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
        }))
        {
            ReportFailure("hook_ctosbox", "Failed to install ctOS timer hook");
            ok = false;
        }
    }
    else
    {
        ReportFailure("hook_ctosbox", "ctOS timer AOB not found");
        ok = false;
    }

    // Timer fixer: 0F 2F 77 04 F3 0F 11 77 08
    uptr addrFix = Memory::ScanDisrupt("0F 2F 77 04 F3 0F 11 77 08");
    if (addrFix)
    {
        if (!InstallMidHook(addrFix, 14, [](uptr cave, uptr returnAddr) {
            u8* p = reinterpret_cast<u8*>(cave);
            size_t i = 0;
            p[i++] = 0x41; p[i++] = 0x57;
            p[i++] = 0x49; p[i++] = 0xBF;
            *reinterpret_cast<u64*>(p + i) = reinterpret_cast<u64>(&g_ptrs.pTimeFixer);
            i += 8;
            p[i++] = 0x49; p[i++] = 0x89; p[i++] = 0x3F;
            p[i++] = 0x41; p[i++] = 0x5F;
            // original: comiss xmm6,[rdi+4]; movss [rdi+8],xmm6
            p[i++] = 0x0F; p[i++] = 0x2F; p[i++] = 0x77; p[i++] = 0x04;
            p[i++] = 0xF3; p[i++] = 0x0F; p[i++] = 0x11; p[i++] = 0x77; p[i++] = 0x08;
            p[i++] = 0xFF; p[i++] = 0x25;
            *reinterpret_cast<u32*>(p + i) = 0; i += 4;
            *reinterpret_cast<u64*>(p + i) = returnAddr; i += 8;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cave), i);
        }))
        {
            ReportFailure("hook_timerfix", "Failed to install TimerFix hook");
            ok = false;
        }
    }
    else
    {
        ReportFailure("hook_timerfix", "TimerFix AOB not found");
        ok = false;
    }

    LOG("Misc timer hooks installed");
    return ok;
}

// -----------------------------------------------------------------------
bool Hooks::InstallAll()
{
    ClearFailures();
    MH_Initialize();

    bool ok = true;
    ok &= InstallGodMode();
    ok &= InstallFocus();
    ok &= InstallBattery();
    ok &= InstallAmmo();
    ok &= InstallCrafting();
    ok &= InstallCoordinates();
    ok &= InstallHeatLevel();
    ok &= InstallHackTime();
    ok &= InstallCarHealth();
    ok &= InstallTimeOfDay();
    ok &= InstallFOV();
    InstallReputation();     // non-fatal
    InstallSpiderTank();     // non-fatal
    InstallNVZN();           // non-fatal
    InstallCashRun();        // non-fatal
    InstallTimers();         // non-fatal
    InstallStealth();        // non-fatal

    g_initialized.store(ok);
    LOG("All hooks installed. Critical hooks ok=%d", (int)ok);
    return ok;
}

void Hooks::UninstallAll()
{
    // Hooks use raw cave JMPs — no MinHook needed to uninstall
    // In practice the game exits with us so restoration is minimal
    MH_Uninitialize();
    g_hooks.clear();
    g_initialized.store(false);
    ClearFailures();
    LOG("All hooks uninstalled");
}
