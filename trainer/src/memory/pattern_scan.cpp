#include "pattern_scan.h"
#include "memory.h"
#include <cctype>
#include <charconv>

namespace Memory
{
    // -----------------------------------------------------------------------
    Pattern::Pattern(const char* pattern)
    {
        const char* p = pattern;
        while (*p)
        {
            while (*p == ' ' || *p == '\t') ++p;
            if (!*p) break;

            if (p[0] == '?' )
            {
                bytes.push_back({0, true});
                ++p;
                if (*p == '?') ++p;
            }
            else
            {
                // parse two hex nibbles
                u8 val = 0;
                auto [ptr, ec] = std::from_chars(p, p + 2, val, 16);
                if (ec == std::errc{})
                {
                    bytes.push_back({val, false});
                    p = ptr;
                }
                else
                {
                    ++p; // skip unknown char
                }
            }
        }
    }

    bool Pattern::Match(const u8* data) const
    {
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (!bytes[i].wildcard && data[i] != bytes[i].value)
                return false;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    uptr ScanRegion(uptr start, size_t size, const Pattern& pattern)
    {
        if (!start || !size || pattern.Size() == 0) return 0;

        const size_t patLen = pattern.Size();
        if (size < patLen) return 0;

        const u8* data = reinterpret_cast<const u8*>(start);
        const size_t limit = size - patLen;

        for (size_t i = 0; i <= limit; ++i)
        {
            if (pattern.Match(data + i))
                return start + i;
        }
        return 0;
    }

    uptr ScanModule(uptr moduleBase, const Pattern& pattern)
    {
        if (!moduleBase) return 0;
        size_t moduleSize = GetModuleSize(moduleBase);
        if (!moduleSize) return 0;

        // Walk PE sections — only scan executable sections for speed
        const u8* base = reinterpret_cast<const u8*>(moduleBase);
        auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        auto* nt   = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        auto* sec  = IMAGE_FIRST_SECTION(nt);

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            uptr secStart = moduleBase + sec->VirtualAddress;
            size_t secSize = sec->Misc.VirtualSize;
            uptr result = ScanRegion(secStart, secSize, pattern);
            if (result) return result;
        }
        return 0;
    }

    uptr Scan(uptr moduleBase, const char* pattern)
    {
        Pattern p(pattern);
        return ScanModule(moduleBase, p);
    }

    uptr ScanDisrupt(const char* pattern)
    {
        return Scan(g_baseDisrupt, pattern);
    }

    uptr ScanGame(const char* pattern)
    {
        return Scan(g_baseGame, pattern);
    }
}
