#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>

namespace Injector
{
    using FatalErrorCallback = void (*)();

    struct InjectionResult
    {
        bool success = false;
        DWORD errorCode = 0;
        std::wstring stage;
        std::wstring message;
    };

    // Returns the PID of watch_dogs.exe if running, else 0
    DWORD FindGame();

    // Injects dllPath into the process with the given PID
    InjectionResult Inject(DWORD pid, const std::wstring& dllPath);

    // Block until the game starts, then inject
    void WaitAndInject(const std::wstring& dllPath, FatalErrorCallback onFatalError);
}

namespace LoaderLog
{
    void Write(const std::wstring& message);
    void WriteLastError(const std::wstring& operation, DWORD error);
    std::wstring FormatLastError(DWORD error);
}
