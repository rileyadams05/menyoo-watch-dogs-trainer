#include "noclip.h"
#include "../common.h"
#include "../memory/memory.h"
#include "../memory/pointer_chain.h"
#include "../hooks/hooks.h"
#include "../ui/ui.h"

// -----------------------------------------------------------------------
// Free Roam / No-Clip
//
// Replicates Paul44's Free Roam Lua timer:
//   - Captures player coordinates from pCoord hook
//   - Reads camera yaw/pitch from pYawPitch hook
//   - Moves the player on WASD/EQ relative to camera direction
//   - Speed is configurable (default 0.5 units/frame at 60fps)
//
// Keys (while menu is CLOSED):
//   W/S       = forward / backward
//   A/D       = strafe left / right
//   E/Space   = ascend
//   Q/LCtrl   = descend
//   Shift     = 3× speed boost
// -----------------------------------------------------------------------

static constexpr float BASE_SPEED   = 0.5f;
static constexpr float BOOST_MULT   = 5.0f;
static constexpr float PI           = 3.14159265358979f;

// Saved coords for restore
static float s_origX = 0.0f, s_origY = 0.0f, s_origZ = 0.0f;
static bool  s_savedOrig = false;
static bool  s_active    = false;

static inline float DegToRad(float deg) { return deg * (PI / 180.0f); }

static inline bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

void NoclipEnable()
{
    s_savedOrig = false;
    s_active    = true;
}

// -----------------------------------------------------------------------
// Called every game frame by CheatManager::Tick() when noclip is active
// -----------------------------------------------------------------------
void NoclipTick()
{
    if (!s_active) return;

    // Don't process movement while the menu is open
    if (UI::IsMenuOpen()) return;

    if (!g_ptrs.pCoord) return;
    uptr coordPtr = Memory::Read<uptr>(g_ptrs.pCoord);
    if (!coordPtr) return;

    // Save original position the first time we activate
    if (!s_savedOrig)
    {
        Memory::SafeRead<float>(coordPtr + 0x0, s_origX);
        Memory::SafeRead<float>(coordPtr + 0x4, s_origY);
        Memory::SafeRead<float>(coordPtr + 0x8, s_origZ);
        s_savedOrig = true;
    }

    float x = 0, y = 0, z = 0;
    Memory::SafeRead<float>(coordPtr + 0x0, x);
    Memory::SafeRead<float>(coordPtr + 0x4, y);
    Memory::SafeRead<float>(coordPtr + 0x8, z);

    // Read camera yaw from pYawPitch (offset 0x88 from base, as per CT)
    float yawRaw = 0.0f;
    if (g_ptrs.pYawPitch)
    {
        uptr ypBase = Memory::Read<uptr>(g_ptrs.pYawPitch);
        if (ypBase) Memory::SafeRead<float>(ypBase + 0x88, yawRaw);
    }

    float speed = BASE_SPEED;
    if (KeyDown(VK_SHIFT)) speed *= BOOST_MULT;

    // Yaw in game is stored as a raw float in degrees or radians depending on version.
    // Paul44's CT reads pYawPitch+0x88 as the yaw angle (treated as radians here).
    // We use sin/cos to get forward vector in the XZ(Y) plane.
    float fwd_x =  sinf(yawRaw);
    float fwd_y =  cosf(yawRaw);

    float right_x = cosf(yawRaw);
    float right_y = -sinf(yawRaw);

    float dx = 0, dy = 0, dz = 0;

    if (KeyDown('W'))       { dx += fwd_x * speed;   dy += fwd_y * speed; }
    if (KeyDown('S'))       { dx -= fwd_x * speed;   dy -= fwd_y * speed; }
    if (KeyDown('A'))       { dx -= right_x * speed; dy -= right_y * speed; }
    if (KeyDown('D'))       { dx += right_x * speed; dy += right_y * speed; }
    if (KeyDown('E') || KeyDown(VK_SPACE)) { dz += speed; }
    if (KeyDown('Q') || KeyDown(VK_CONTROL)) { dz -= speed; }

    if (dx != 0 || dy != 0 || dz != 0)
    {
        Memory::SafeWrite<float>(coordPtr + 0x0, x + dx);
        Memory::SafeWrite<float>(coordPtr + 0x4, y + dy);
        Memory::SafeWrite<float>(coordPtr + 0x8, z + dz);
    }
}

// -----------------------------------------------------------------------
// Called when noclip is toggled OFF — restore to saved position
// -----------------------------------------------------------------------
void NoclipDisable()
{
    if (!s_active) return;
    if (!s_savedOrig) return;
    if (!g_ptrs.pCoord) return;
    uptr coordPtr = Memory::Read<uptr>(g_ptrs.pCoord);
    if (!coordPtr) return;

    Memory::SafeWrite<float>(coordPtr + 0x0, s_origX);
    Memory::SafeWrite<float>(coordPtr + 0x4, s_origY);
    Memory::SafeWrite<float>(coordPtr + 0x8, s_origZ);
    s_savedOrig = false;
    s_active    = false;
}

