#pragma once

#include <filesystem>
#include <string>

namespace TrainerLog
{
    // Initializes the DLL-side logger. Safe to call multiple times; later
    // calls reopen the log file. The log is written to trainer_dll.log in the
    // provided directory. Any queued messages recorded before initialization
    // are flushed during Init.
    void Init(const std::filesystem::path& dllDirectory);

    // Shuts down the logger and flushes the log file.
    void Shutdown();

    // Writes a formatted message (printf-style) to the log. Automatically
    // prefixes the line with a timestamp. Thread-safe.
    void Write(const char* fmt, ...);

    // Convenience overload for std::string-based messages.
    void Write(const std::string& message);
}
