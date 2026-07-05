#include "pointer_chain.h"
#include "../hooks/hooks.h"

GamePointers g_ptrs{};

// -----------------------------------------------------------------------
// Helper: find the static pointer that Paul44's Lua calls getStaticAddr().
// In the CT, getStaticAddr scans for an instruction pattern and reads the
// RIP-relative pointer encoded in the instruction bytes.
// We replicate this: scan for the pattern, then decode the RIP-relative LEA.
//
// lea rcx, [rip + disp32]  ->  bytes: 48 8D 0D <disp32>
// rip = addr_of_instruction + instruction_len (7 bytes for 64-bit LEA)
// result = rip + disp32
// -----------------------------------------------------------------------
static uptr DecodeRipRelative(uptr instrAddr, int instrLen, int dispOffset = 3)
{
    if (!instrAddr) return 0;
    i32 disp = *reinterpret_cast<i32*>(instrAddr + dispOffset);
    return instrAddr + instrLen + disp;
}

// -----------------------------------------------------------------------
// pChainHealth — Paul44's CT:
//   getStaticAddr("48 89 2D",4,"pChainHealth",0,startAddr,endAddr)
// Finds: mov [rip+disp32], rbp  (48 89 2D <disp32>)
// -----------------------------------------------------------------------
static uptr FindChainHealth()
{
    uptr addr = Memory::ScanDisrupt("48 89 2D ?? ?? ?? ?? 48 8B 05");
    if (!addr) return 0;
    return DecodeRipRelative(addr, 7, 3);
}

// -----------------------------------------------------------------------
// pChainExp — Paul44's CT:
//   getStaticAddr("33 FF 48 89 7D C7 48 89",4,"pChainExp",0,...)
// Finds instruction that stores player pointer into a global
// Pattern: 33 FF 48 89 7D C7 48 89 <xx> <rip-disp>
// -----------------------------------------------------------------------
static uptr FindChainExp()
{
    // Look for: xor edi,edi; mov [rbp+C7h],rdi; mov [rip+disp32], ...
    uptr addr = Memory::ScanDisrupt("33 FF 48 89 7D C7 48 89 3D");
    if (!addr) return 0;
    // offset 7: 48 89 3D <disp32> = mov [rip+disp32], rdi
    return DecodeRipRelative(addr + 7, 7, 3);
}

// -----------------------------------------------------------------------
// pChainRep — repututation chain pointer
//   getStaticAddr("48 8B 0D",4,"pChainRep",0,...) near reputation hook
// -----------------------------------------------------------------------
static uptr FindChainRep()
{
    // Reputation hook: 03 3C BB (add edi,[rbx+rdi*4]) -- reputation accumulator
    // Find via the unique pattern near it
    uptr addr = Memory::ScanDisrupt("48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 FF 50");
    if (!addr) return 0;
    return DecodeRipRelative(addr, 7, 3);
}

// -----------------------------------------------------------------------
// pMapWaypt — Paul44's CT uses a static pattern for the waypoint manager
// -----------------------------------------------------------------------
static uptr FindMapWaypt()
{
    // Look for a mov rax,[rip+disp] that loads the waypoint manager ptr
    // Pattern from CT: near code that accesses E0 offset for waypoints
    uptr addr = Memory::ScanDisrupt("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? F6 80 44");
    if (!addr) return 0;
    return DecodeRipRelative(addr, 7, 3);
}

// -----------------------------------------------------------------------
// pChainProgr — progression challenge chain
// -----------------------------------------------------------------------
static uptr FindChainProgr()
{
    // Pattern: 33 FF 48 89 7D C7 48 89 (same region as Chain-search above, but different result)
    // Paul44: getStaticAddr("33 FF 48 89 7D C7 48 89",4,"pChainProgr",0,...)
    // This points to the progression manager base
    uptr addr = Memory::ScanDisrupt("4C 8B 35 ?? ?? ?? ?? 49 8B 1E");
    if (!addr) return 0;
    return DecodeRipRelative(addr, 7, 3);
}

// -----------------------------------------------------------------------
bool ResolveGamePointers()
{
    if (!g_baseDisrupt) return false;

    g_ptrs.Reset();

    // Static chains (Lua-equivalent AOB)
    g_ptrs.pChainHealth = FindChainHealth();
    g_ptrs.pChainExp    = FindChainExp();
    g_ptrs.pChainRep    = FindChainRep();
    g_ptrs.pMapWaypt    = FindMapWaypt();
    g_ptrs.pChainProgr  = FindChainProgr();

    LOG("pChainHealth = %p", (void*)g_ptrs.pChainHealth);
    LOG("pChainExp    = %p", (void*)g_ptrs.pChainExp);
    LOG("pChainRep    = %p", (void*)g_ptrs.pChainRep);

    if (g_ptrs.pMapWaypt)
        Hooks::ClearFailure("hook_waypoint");
    else
        Hooks::ReportFailure("hook_waypoint", "Waypoint pointer not resolved");

    // Dynamic pointers (pHealth, pFocus, etc.) are filled by hooks at runtime
    // We mark valid=true to indicate the static portion is resolved
    g_ptrs.valid = (g_ptrs.pChainHealth != 0 || g_ptrs.pChainExp != 0);
    return g_ptrs.valid;
}
