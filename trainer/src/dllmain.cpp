#include "common.h"
#include "diagnostics/diagnostics.h"
#include "process/process.h"
#include "ui/ui.h"

#include <filesystem>

namespace
{
    DWORD WINAPI BootstrapThread(LPVOID)
    {
        wchar_t modulePath[MAX_PATH]{};
        if (GetModuleFileNameW(g_hModule, modulePath, MAX_PATH) == 0)
        {
            TrainerLog::Write("Bootstrap: GetModuleFileNameW failed (%lu)", GetLastError());
            return 0;
        }

        std::filesystem::path dllPath(modulePath);
        auto dllDir = dllPath.parent_path();

        TrainerLog::Init(dllDir);
        LOG("Bootstrap thread started");
        LOG("DLL path: %ws", dllPath.wstring().c_str());
        LOG("Pointer size: %zu-bit", sizeof(void*) * 8);

        Diagnostics::Load(dllDir);

        Process::StartWatchThread();
        LOG("Bootstrap thread completed initialization");
        return 0;
    }
}

// -----------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_hModule = hModule;
        TrainerLog::Write("DllMain: PROCESS_ATTACH entered");

        if (!DisableThreadLibraryCalls(hModule))
            TrainerLog::Write("DllMain: DisableThreadLibraryCalls failed (%lu)", GetLastError());
        else
            TrainerLog::Write("DllMain: DisableThreadLibraryCalls succeeded");

        HANDLE hThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (hThread)
        {
            CloseHandle(hThread);
        }
        else
        {
            TrainerLog::Write("DllMain: Failed to create bootstrap thread (%lu)", GetLastError());
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        TrainerLog::Write("DllMain: PROCESS_DETACH entered");
        UI::Shutdown();
        Process::Shutdown();
        TrainerLog::Shutdown();
        break;
    }
    return TRUE;
}
