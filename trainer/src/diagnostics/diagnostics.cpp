#include "diagnostics.h"

#include "../common.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace
{
    DiagnosticsConfig             g_config{};
    std::filesystem::path         g_configPath;
    bool                          g_loaded = false;

    DiagnosticsConfig ParseConfig(const nlohmann::json& j)
    {
        DiagnosticsConfig cfg;

        auto getBool = [&](const char* key, bool& target)
        {
            if (j.contains(key) && j[key].is_boolean())
                target = j[key].get<bool>();
        };

        getBool("diagnosticMode",      cfg.diagnosticMode);
        getBool("enableOverlay",       cfg.enableOverlay);
        getBool("enableHooks",         cfg.enableHooks);
        getBool("enableCheats",        cfg.enableCheats);
        getBool("enableMemoryPatches", cfg.enableMemoryPatches);
        getBool("enablePatternScan",   cfg.enablePatternScan);
        getBool("enableHotkeys",       cfg.enableHotkeys);
        getBool("enablePipeOrIpc",     cfg.enablePipeOrIpc);

        return cfg;
    }
}

namespace Diagnostics
{
    void Load(const std::filesystem::path& dllDirectory)
    {
        g_config = DiagnosticsConfig{}; // reset to defaults (safe diagnostic mode)
        g_configPath = dllDirectory / "trainer_debug.json";

        LOG("Diagnostics: loading configuration from %ws", g_configPath.wstring().c_str());

        std::ifstream file(g_configPath);
        if (!file.is_open())
        {
            LOG("Diagnostics: configuration file not found. Using defaults (diagnostic mode).");
            g_loaded = false;
            return;
        }

        try
        {
            nlohmann::json j;
            file >> j;
            g_config = ParseConfig(j);
            g_loaded = true;
        }
        catch (const std::exception& e)
        {
            LOG("Diagnostics: failed to parse trainer_debug.json (%s). Using defaults.", e.what());
            g_loaded = false;
        }

        LogCurrentConfig();
    }

    const DiagnosticsConfig& GetConfig()
    {
        return g_config;
    }

    void LogCurrentConfig()
    {
        LOG("Diagnostics: diagnosticMode=%d", g_config.diagnosticMode ? 1 : 0);
        LOG("Diagnostics: enableOverlay=%d", g_config.enableOverlay ? 1 : 0);
        LOG("Diagnostics: enableHooks=%d", g_config.enableHooks ? 1 : 0);
        LOG("Diagnostics: enableCheats=%d", g_config.enableCheats ? 1 : 0);
        LOG("Diagnostics: enableMemoryPatches=%d", g_config.enableMemoryPatches ? 1 : 0);
        LOG("Diagnostics: enablePatternScan=%d", g_config.enablePatternScan ? 1 : 0);
        LOG("Diagnostics: enableHotkeys=%d", g_config.enableHotkeys ? 1 : 0);
        LOG("Diagnostics: enablePipeOrIpc=%d", g_config.enablePipeOrIpc ? 1 : 0);
    }

    const std::filesystem::path& GetConfigPath()
    {
        return g_configPath;
    }
}
