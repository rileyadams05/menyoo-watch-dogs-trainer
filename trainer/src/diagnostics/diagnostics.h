#pragma once

#include <filesystem>

struct DiagnosticsConfig
{
    bool diagnosticMode      = true;
    bool enableOverlay       = false;
    bool enableHooks         = false;
    bool enableCheats        = false;
    bool enableMemoryPatches = false;
    bool enablePatternScan   = false;
    bool enableHotkeys       = false;
    bool enablePipeOrIpc     = true;
};

namespace Diagnostics
{
    // Loads diagnostic settings from trainer_debug.json located alongside the
    // DLL. If the file is missing, defaults remain (safe diagnostic mode).
    void Load(const std::filesystem::path& dllDirectory);

    // Returns the active diagnostic configuration.
    const DiagnosticsConfig& GetConfig();

    // Writes the current configuration to the log for visibility.
    void LogCurrentConfig();

    // Returns the last path used for trainer_debug.json.
    const std::filesystem::path& GetConfigPath();
}
