#include "injector.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <thread>
#include <chrono>
#include <string>
#include <cstdint>

static constexpr WCHAR TARGET_EXE[] = L"watch_dogs.exe";

// -----------------------------------------------------------------------
static Injector::InjectionResult Failure(const std::wstring& stage, DWORD errorCode, const std::wstring& detail = L"")
{
    Injector::InjectionResult result{};
    result.success = false;
    result.errorCode = errorCode;
    result.stage = stage;
    result.message = detail.empty() ? LoaderLog::FormatLastError(errorCode) : detail;
    return result;
}

// -----------------------------------------------------------------------
DWORD Injector::FindGame()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
    {
        LoaderLog::WriteLastError(L"CreateToolhelp32Snapshot failed", GetLastError());
        return 0;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
    {
        do {
            if (_wcsicmp(pe.szExeFile, TARGET_EXE) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    else
    {
        LoaderLog::WriteLastError(L"Process32FirstW failed", GetLastError());
    }

    CloseHandle(snap);

    return pid;
}

// -----------------------------------------------------------------------
Injector::InjectionResult Injector::Inject(DWORD pid, const std::wstring& dllPath)
{
    LoaderLog::Write(L"OpenProcess starting. PID: " + std::to_wstring(pid));

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION  | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);

    if (!hProcess)
    {
        DWORD error = GetLastError();
        LoaderLog::WriteLastError(L"OpenProcess failed", error);
        return Failure(L"OpenProcess", error);
    }

    LoaderLog::Write(L"OpenProcess success.");

    // Allocate memory for DLL path string in the target process
    size_t pathSize = (dllPath.size() + 1) * sizeof(wchar_t);
    LoaderLog::Write(L"VirtualAllocEx starting. Bytes: " + std::to_wstring(pathSize));

    LPVOID remote = VirtualAllocEx(hProcess, nullptr, pathSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!remote)
    {
        DWORD error = GetLastError();
        LoaderLog::WriteLastError(L"VirtualAllocEx failed", error);
        CloseHandle(hProcess);
        return Failure(L"VirtualAllocEx", error);
    }

    LoaderLog::Write(L"VirtualAllocEx success. Remote address: " + std::to_wstring(reinterpret_cast<uintptr_t>(remote)));

    LoaderLog::Write(L"WriteProcessMemory starting.");
    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(hProcess, remote, dllPath.c_str(), pathSize, &bytesWritten))
    {
        DWORD error = GetLastError();
        LoaderLog::WriteLastError(L"WriteProcessMemory failed", error);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Failure(L"WriteProcessMemory", error);
    }

    if (bytesWritten != pathSize)
    {
        std::wstring detail = L"Wrote " + std::to_wstring(bytesWritten) +
            L" of " + std::to_wstring(pathSize) + L" bytes.";
        LoaderLog::Write(L"WriteProcessMemory failed. " + detail);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Failure(L"WriteProcessMemory", ERROR_PARTIAL_COPY, detail);
    }

    LoaderLog::Write(L"WriteProcessMemory success. Bytes written: " + std::to_wstring(bytesWritten));

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32)
    {
        DWORD error = GetLastError();
        LoaderLog::WriteLastError(L"GetModuleHandleW(kernel32.dll) failed", error);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Failure(L"GetModuleHandleW", error);
    }

    FARPROC pLoadLib  = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLib)
    {
        DWORD error = GetLastError();
        LoaderLog::WriteLastError(L"GetProcAddress(LoadLibraryW) failed", error);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Failure(L"GetProcAddress", error);
    }

    LoaderLog::Write(L"CreateRemoteThread starting.");
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLib),
        remote, 0, nullptr);

    bool success = false;
    if (hThread)
    {
        LoaderLog::Write(L"CreateRemoteThread success.");
        WaitForSingleObject(hThread, 8000);
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        success = (exitCode != 0);
        if (success)
        {
            LoaderLog::Write(L"Injection succeeded. LoadLibraryW returned module handle: " + std::to_wstring(exitCode));
        }
        else
        {
            LoaderLog::Write(L"Injection failed. LoadLibraryW returned 0 in the target process.");
        }
        CloseHandle(hThread);
    }
    else
    {
        DWORD error = GetLastError();
        LoaderLog::WriteLastError(L"CreateRemoteThread failed", error);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return Failure(L"CreateRemoteThread", error);
    }

    VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (!success)
        return Failure(L"LoadLibraryW", ERROR_DLL_INIT_FAILED, L"LoadLibraryW returned 0 in the target process.");

    Injector::InjectionResult result{};
    result.success = true;
    result.message = L"Trainer loaded";
    return result;
}

// -----------------------------------------------------------------------
void Injector::WaitAndInject(const std::wstring& dllPath, FatalErrorCallback onFatalError)
{
    // Track PIDs we've already injected to avoid double injection
    DWORD lastInjectedPid = 0;

    for (;;)
    {
        DWORD pid = FindGame();

        if (pid && pid != lastInjectedPid)
        {
            // Wait a moment for the game to load its DLLs
            std::this_thread::sleep_for(std::chrono::seconds(3));

            InjectionResult result = Inject(pid, dllPath);
            if (result.success)
            {
                lastInjectedPid = pid;
            }
            else
            {
                LoaderLog::Write(L"Injection failed. The loader will stay open for review.");
                if (onFatalError)
                    onFatalError();
                return;
            }
        }
        else if (!pid)
        {
            // Game closed — reset so we can re-inject on next launch
            lastInjectedPid = 0;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
