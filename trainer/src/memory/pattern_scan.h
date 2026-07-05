#pragma once
#include "../common.h"

namespace Memory
{
    // -----------------------------------------------------------------------
    // AOB (Array of Bytes) pattern scanner
    //
    // Pattern format: hex bytes separated by spaces, '?' or '??' = wildcard
    // Examples:
    //   "F3 0F 10 41 18 ? ? 5C 41 1C"
    //   "44 88 ?? ?? 48 8B 8E D0 00 00 00"
    // -----------------------------------------------------------------------

    struct Pattern
    {
        struct Byte { u8 value; bool wildcard; };
        std::vector<Byte> bytes;

        explicit Pattern(const char* pattern);
        bool Match(const u8* data) const;
        size_t Size() const { return bytes.size(); }
    };

    // Scan a memory region — returns first match or 0
    uptr ScanRegion(uptr start, size_t size, const Pattern& pattern);

    // Scan entire module
    uptr ScanModule(uptr moduleBase, const Pattern& pattern);

    // Convenience wrappers
    uptr Scan(uptr moduleBase, const char* pattern);
    uptr ScanDisrupt(const char* pattern);   // scans Disrupt_b64.dll
    uptr ScanGame(const char* pattern);      // scans watch_dogs.exe
}
