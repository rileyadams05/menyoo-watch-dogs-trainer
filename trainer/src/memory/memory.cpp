#include "memory.h"
#include <Psapi.h>

namespace Memory
{
    // -----------------------------------------------------------------------
    bool Patch(uptr address, const u8* bytes, size_t count)
    {
        if (!address || !bytes || !count) return false;
        ScopedProtect sp(address, count);
        memcpy(reinterpret_cast<void*>(address), bytes, count);
        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<LPCVOID>(address), count);
        return true;
    }

    bool PatchNop(uptr address, size_t count)
    {
        std::vector<u8> nops(count, 0x90);
        return Patch(address, nops.data(), count);
    }

    // -----------------------------------------------------------------------
    bool PatchGuard::Apply(uptr addr, const u8* newBytes, size_t count)
    {
        if (active) Restore();
        address = addr;
        original.resize(count);
        memcpy(original.data(), reinterpret_cast<void*>(addr), count);
        active = Patch(addr, newBytes, count);
        return active;
    }

    void PatchGuard::Restore()
    {
        if (!active || !address || original.empty()) return;
        Patch(address, original.data(), original.size());
        active = false;
    }

    // -----------------------------------------------------------------------
    ScopedProtect::ScopedProtect(uptr addr, size_t sz, DWORD newProtect)
        : address(addr), size(sz)
    {
        VirtualProtect(reinterpret_cast<LPVOID>(addr), sz, newProtect, &oldProtect);
    }

    ScopedProtect::~ScopedProtect()
    {
        DWORD tmp;
        VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &tmp);
    }

    // -----------------------------------------------------------------------
    uptr ResolveChain(uptr base, const std::vector<uptr>& offsets)
    {
        uptr addr = base;
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            if (!addr) return 0;
            if (i < offsets.size() - 1)
                addr = *reinterpret_cast<uptr*>(addr + offsets[i]);
            else
                addr = addr + offsets[i];
        }
        return addr;
    }

    uptr ResolveChainSafe(uptr base, const std::vector<uptr>& offsets)
    {
        uptr addr = base;
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            if (!addr) return 0;
            if (i < offsets.size() - 1)
            {
                uptr next = 0;
                if (!SafeRead<uptr>(addr + offsets[i], next)) return 0;
                addr = next;
            }
            else
            {
                addr = addr + offsets[i];
            }
        }
        return addr;
    }

    // -----------------------------------------------------------------------
    uptr AllocNear(uptr target, size_t size)
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);

        uptr lo = (target > 0x40000000ULL) ? target - 0x40000000ULL : 0;
        uptr hi = target + 0x40000000ULL;
        lo = (lo + si.dwAllocationGranularity - 1) & ~(uptr)(si.dwAllocationGranularity - 1);

        for (uptr addr = lo; addr < hi; addr += si.dwAllocationGranularity)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
                continue;
            if (mbi.State == MEM_FREE)
            {
                void* p = VirtualAlloc(reinterpret_cast<LPVOID>(addr), size,
                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (p) return reinterpret_cast<uptr>(p);
            }
        }
        // Fallback: anywhere
        void* p = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        return reinterpret_cast<uptr>(p);
    }

    void FreeAlloc(uptr address, size_t /*size*/)
    {
        if (address)
            VirtualFree(reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE);
    }

    // -----------------------------------------------------------------------
    // 14-byte absolute JMP: FF 25 00 00 00 00 <8-byte addr>
    void WriteAbsJmp(uptr src, uptr dst)
    {
        ScopedProtect sp(src, 14);
        u8* p = reinterpret_cast<u8*>(src);
        p[0] = 0xFF; p[1] = 0x25;
        *reinterpret_cast<u32*>(p + 2) = 0;
        *reinterpret_cast<u64*>(p + 6) = dst;
        FlushInstructionCache(GetCurrentProcess(), p, 14);
    }

    // E8 relative CALL (only works if |dst - src - 5| < 2GB)
    void WriteCall(uptr src, uptr dst)
    {
        ScopedProtect sp(src, 5);
        u8* p = reinterpret_cast<u8*>(src);
        p[0] = 0xE8;
        *reinterpret_cast<i32*>(p + 1) = static_cast<i32>(dst - src - 5);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
    }

    // -----------------------------------------------------------------------
    uptr GetModuleBase(const char* moduleName)
    {
        HMODULE h = GetModuleHandleA(moduleName);
        return h ? reinterpret_cast<uptr>(h) : 0;
    }

    size_t GetModuleSize(uptr base)
    {
        if (!base) return 0;
        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(),
            reinterpret_cast<HMODULE>(base), &mi, sizeof(mi));
        return mi.SizeOfImage;
    }
}
