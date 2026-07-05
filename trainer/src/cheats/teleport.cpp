#include "../common.h"
#include "../memory/memory.h"
#include "../memory/pointer_chain.h"

float g_savedX = 0.0f;
float g_savedY = 0.0f;
float g_savedZ = 0.0f;

// -----------------------------------------------------------------------
// Resolve map waypoint coordinates from the waypoint manager pointer.
// Replicates Paul44's Lua teleport-to-waypoint logic:
//
//   addrWaypoint = [[[[pMapWaypt]+0xE0]+0x88]+0x280]
//   bWaypoint    = [addrWaypoint+0x44]   (1 = waypoint is set)
//   bTypeWaypt   = [addrWaypoint+0x68]
//   addrGPS      = [addrWaypoint+0x80]
//   xOffset      = (bTypeWaypt != 8 && [addrGPS+0x1C] != 0) ? 0x18 : 0x20
//   MapX/Y/Z     = [addrGPS+xOffset], [+4], [+8]
// -----------------------------------------------------------------------
void DoWaypointTeleport()
{
    if (!g_ptrs.pMapWaypt) return;

    uptr base = Memory::Read<uptr>(g_ptrs.pMapWaypt);
    if (!base) return;

    uptr step1 = 0, step2 = 0, step3 = 0, waypoint = 0;
    if (!Memory::SafeRead<uptr>(base + 0xE0, step1) || !step1) return;
    if (!Memory::SafeRead<uptr>(step1 + 0x88, step2) || !step2) return;
    if (!Memory::SafeRead<uptr>(step2 + 0x280, step3) || !step3) return;
    waypoint = step3;

    u8 bWaypoint = 0;
    Memory::SafeRead<u8>(waypoint + 0x44, bWaypoint);
    if (bWaypoint != 1) return;  // no waypoint set

    u8 bType = 0;
    Memory::SafeRead<u8>(waypoint + 0x68, bType);

    uptr addrGPS = 0;
    Memory::SafeRead<uptr>(waypoint + 0x80, addrGPS);
    if (!addrGPS) return;

    uptr xOffset = 0x20;
    float checkVal = 0.0f;
    Memory::SafeRead<float>(addrGPS + 0x1C, checkVal);
    if (bType != 8 && checkVal != 0.0f) xOffset = 0x18;

    float mapX = 0.0f, mapY = 0.0f, mapZ = 0.0f;
    Memory::SafeRead<float>(addrGPS + xOffset,       mapX);
    Memory::SafeRead<float>(addrGPS + xOffset + 0x4, mapY);
    Memory::SafeRead<float>(addrGPS + xOffset + 0x8, mapZ);

    // Get player coordinate pointer
    if (!g_ptrs.pCoord) return;
    uptr coordPtr = Memory::Read<uptr>(g_ptrs.pCoord);
    if (!coordPtr) return;

    // Apply drop height (3.0f above waypoint Z to avoid going underground)
    float dropH = 3.0f;
    mapZ = (mapZ + dropH > mapZ) ? mapZ + dropH : mapZ;

    Memory::SafeWrite<float>(coordPtr + 0x0, mapX);
    Memory::SafeWrite<float>(coordPtr + 0x4, mapY);
    Memory::SafeWrite<float>(coordPtr + 0x8, mapZ);

    LOG("Teleported to waypoint: %.2f %.2f %.2f", mapX, mapY, mapZ);
}
