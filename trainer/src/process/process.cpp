#include "process.h"
#include "../diagnostics/diagnostics.h"
#include "../memory/memory.h"
#include "../memory/pointer_chain.h"
#include "../hooks/hooks.h"
#include "../hooks/dx11_hook.h"
#include "../cheats/cheat_manager.h"
#include "../config/config.h"
#include <mutex>

static std::atomic<bool> g_running    { false };
static std::atomic<bool> g_gameReady  { false };
static std::thread       g_watchThread;
static std::once_flag    g_registerCheatsOnce;
static std::once_flag    g_loadConfigOnce;
static DiagnosticsConfig g_options{};
static std::atomic<bool> g_modulesLogged{ false };

// -----------------------------------------------------------------------
// Full initialization sequence — runs once Disrupt_b64.dll is present
// -----------------------------------------------------------------------
static void Initialize()
{
    g_gameReady = false;
    LOG("Process::Initialize: begin (diagnosticMode=%d)", g_options.diagnosticMode ? 1 : 0);

    // Grab module bases
    g_baseGame    = Memory::GetModuleBase("watch_dogs.exe");
    g_baseDisrupt = Memory::GetModuleBase("Disrupt_b64.dll");

    if (!g_baseDisrupt)
    {
        LOG("Process::Initialize: Disrupt_b64.dll not found — deferring init");
        return;
    }

    if (!g_modulesLogged.exchange(true))
    {
        LOG("Process::Initialize: game base    = %p", (void*)g_baseGame);
        LOG("Process::Initialize: disrupt base = %p", (void*)g_baseDisrupt);
    }

    if (g_options.enablePatternScan)
    {
        LOG("Process::Initialize: pattern scan starting");
        bool pointersOk = ResolveGamePointers();
        LOG("Process::Initialize: pattern scan %s", pointersOk ? "succeeded" : "incomplete");
    }
    else
    {
        LOG("Process::Initialize: pattern scan disabled by diagnostic configuration");
    }

    bool hooksOk = true;
    if (g_options.enableHooks)
    {
        LOG("Process::Initialize: installing hooks");
        hooksOk = Hooks::IsInitialized() ? true : Hooks::InstallAll();
        LOG("Process::Initialize: hooks %s", hooksOk ? "installed" : "failed");
        if (!hooksOk)
        {
            LOG("Process::Initialize: hook installation failed; will retry");
            return;
        }
    }
    else
    {
        LOG("Process::Initialize: hooks disabled by diagnostic configuration");
        Hooks::UninstallAll();
    }

    bool dxOk = true;
    if (g_options.enableOverlay)
    {
        LOG("Process::Initialize: installing DX11 hook");
        dxOk = DX11Hook::IsReady() ? true : DX11Hook::Install();
        LOG("Process::Initialize: DX11 hook %s", dxOk ? "installed" : "failed");
        if (!dxOk)
        {
            LOG("Process::Initialize: DX11 hook install failed; will retry");
            return;
        }
    }
    else
    {
        LOG("Process::Initialize: overlay/DX11 hook disabled by diagnostic configuration");
        DX11Hook::Uninstall();
    }

    if (g_options.enablePatternScan && (g_options.enableHooks || g_options.enableOverlay))
    {
        LOG("Process::Initialize: refreshing pointers after hooks");
        ResolveGamePointers();
    }

    if (g_options.enableCheats)
    {
        std::call_once(g_registerCheatsOnce, []()
        {
            LOG("Process::Initialize: registering cheats");
            CheatManager::Get().RegisterAll();
        });

        if (g_options.enableMemoryPatches)
        {
            std::call_once(g_loadConfigOnce, []()
            {
                LOG("Process::Initialize: loading trainer config");
                Config::Get().Load();
            });

            auto& cheats = CheatManager::Get().GetCheats();
            LOG("Process::Initialize: applying cached cheat states");
            Config::Get().ApplyAll(cheats);
        }
        else
        {
            LOG("Process::Initialize: memory patches disabled; skipping Config::ApplyAll");
        }
    }
    else
    {
        LOG("Process::Initialize: cheats disabled by diagnostic configuration");
    }

    g_gameReady = true;
    LOG("Process::Initialize: completed successfully");
}

// -----------------------------------------------------------------------
// Watch thread — polls for Disrupt_b64.dll once per second
// -----------------------------------------------------------------------
static void WatchThreadProc()
{
    LOG("WatchThread: started");
    while (g_running)
    {
        if (!g_gameReady)
        {
            uptr disrupt = Memory::GetModuleBase("Disrupt_b64.dll");
            if (disrupt)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                Initialize();
            }
        }
        else
        {
            if (g_options.enableCheats)
                CheatManager::Get().Tick();

            if (g_options.enablePatternScan && g_ptrs.pCoord)
            {
                uptr test = Memory::Read<uptr>(g_ptrs.pCoord);
                if (!test)
                {
                    ResolveGamePointers();
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    LOG("WatchThread: exiting");
}

// -----------------------------------------------------------------------
void Process::StartWatchThread()
{
    if (g_running)
    {
        LOG("Process::StartWatchThread: already running");
        return;
    }

    g_options = Diagnostics::GetConfig();
    g_running = true;
    g_watchThread = std::thread(WatchThreadProc);
    LOG("Process::StartWatchThread: watch thread created (diagnosticMode=%d)", g_options.diagnosticMode ? 1 : 0);
}

void Process::Shutdown()
{
    g_running   = false;
    g_gameReady = false;

    if (g_watchThread.joinable())
        g_watchThread.join();

    if (g_options.enableHooks)
        Hooks::UninstallAll();
    if (g_options.enableOverlay)
        DX11Hook::Uninstall();

    LOG("Process::Shutdown: completed");
}

bool Process::IsGameReady() { return g_gameReady; }
