#pragma once
#include "../common.h"

namespace Process
{
    // Called once from DllMain — waits for Disrupt_b64.dll to be loaded
    // then kicks off full initialization on a background thread.
    void StartWatchThread();
    void Shutdown();

    bool IsGameReady();
}
