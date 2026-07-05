#include "logger.h"

#include <Windows.h>
#include <chrono>
#include <cstdarg>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    std::mutex                             g_mutex;
    std::ofstream                          g_stream;
    bool                                   g_initialized = false;
    std::filesystem::path                  g_logPath;
    std::vector<std::string>               g_pending;

    std::string BuildTimestamp()
    {
        using namespace std::chrono;
        const auto now       = system_clock::now();
        const auto timeT     = system_clock::to_time_t(now);
        const auto millis    = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        tm localTm{};
        localtime_s(&localTm, &timeT);

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            1900 + localTm.tm_year,
            1 + localTm.tm_mon,
            localTm.tm_mday,
            localTm.tm_hour,
            localTm.tm_min,
            localTm.tm_sec,
            static_cast<int>(millis.count()));
        return std::string(buffer);
    }

    void AppendLine(const std::string& line)
    {
        std::string timestamped = "[" + BuildTimestamp() + "] " + line + "\n";

        if (g_initialized && g_stream.is_open())
        {
            g_stream << timestamped;
            g_stream.flush();
        }
        else
        {
            g_pending.push_back(timestamped);
        }

#ifdef _DEBUG
        OutputDebugStringA(timestamped.c_str());
#else
        OutputDebugStringA(timestamped.c_str());
#endif
    }
}

namespace TrainerLog
{
    void Init(const std::filesystem::path& dllDirectory)
    {
        std::scoped_lock lock(g_mutex);

        g_logPath = dllDirectory / "trainer_dll.log";

        g_stream.close();
        g_stream.clear();
        g_stream.open(g_logPath, std::ios::out | std::ios::trunc | std::ios::binary);

        g_initialized = g_stream.is_open();

        if (g_initialized)
        {
            for (const auto& pending : g_pending)
            {
                g_stream << pending;
            }
            g_stream.flush();
            g_pending.clear();
        }
    }

    void Shutdown()
    {
        std::scoped_lock lock(g_mutex);
        if (g_stream.is_open())
        {
            g_stream.flush();
            g_stream.close();
        }
        g_initialized = false;
    }

    void Write(const std::string& message)
    {
        std::scoped_lock lock(g_mutex);
        AppendLine(message);
    }

    void Write(const char* fmt, ...)
    {
        std::scoped_lock lock(g_mutex);

        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        AppendLine(buffer);
    }
}
