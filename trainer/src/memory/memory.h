#pragma once
#include "../common.h"

namespace Memory
{
    // -----------------------------------------------------------------------
    // Raw read / write helpers (in-process — trainer runs as injected DLL)
    // -----------------------------------------------------------------------
    template<typename T>
    inline T Read(uptr address)
    {
        if (!address) return T{};
        return *reinterpret_cast<T*>(address);
    }

    template<typename T>
    inline void Write(uptr address, T value)
    {
        if (!address) return;
        *reinterpret_cast<T*>(address) = value;
    }

    // Safe variants that guard against access violations via structured exception
    template<typename T>
    inline bool SafeRead(uptr address, T& out)
    {
        __try {
            out = *reinterpret_cast<T*>(address);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template<typename T>
    inline bool SafeWrite(uptr address, T value)
    {
        __try {
            *reinterpret_cast<T*>(address) = value;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // -----------------------------------------------------------------------
    // Patch helpers — temporarily or permanently overwrite bytes
    // -----------------------------------------------------------------------
    bool Patch(uptr address, const u8* bytes, size_t count);
    bool PatchNop(uptr address, size_t count);

    // Stores original bytes and can restore them
    struct PatchGuard
    {
        uptr            address = 0;
        std::vector<u8> original;
        bool            active  = false;

        bool Apply(uptr addr, const u8* newBytes, size_t count);
        void Restore();
        ~PatchGuard() { Restore(); }
    };

    // -----------------------------------------------------------------------
    // VirtualProtect RAII wrapper
    // -----------------------------------------------------------------------
    struct ScopedProtect
    {
        uptr   address;
        size_t size;
        DWORD  oldProtect = 0;

        ScopedProtect(uptr addr, size_t sz, DWORD newProtect = PAGE_EXECUTE_READWRITE);
        ~ScopedProtect();
    };

    // -----------------------------------------------------------------------
    // Pointer chain resolution
    // e.g. ResolveChain(base, {0x10, 0x78, 0x98}) reads:
    //     ptr = *(base + 0x10), ptr = *(ptr + 0x78), result = ptr + 0x98
    // -----------------------------------------------------------------------
    uptr ResolveChain(uptr base, const std::vector<uptr>& offsets);
    uptr ResolveChainSafe(uptr base, const std::vector<uptr>& offsets);

    // -----------------------------------------------------------------------
    // Allocate / free executable memory near a target address
    // -----------------------------------------------------------------------
    uptr AllocNear(uptr target, size_t size);
    void FreeAlloc(uptr address, size_t size);

    // -----------------------------------------------------------------------
    // Call gate — write a 14-byte absolute JMP to dst at src
    // -----------------------------------------------------------------------
    void WriteAbsJmp(uptr src, uptr dst);
    void WriteCall(uptr src, uptr dst);   // E8 relative call (≤2GB range)

    // -----------------------------------------------------------------------
    // Module helpers
    // -----------------------------------------------------------------------
    uptr GetModuleBase(const char* moduleName);
    size_t GetModuleSize(uptr base);
}
