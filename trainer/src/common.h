#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <optional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <memory>
#include <cassert>

#include "logging/logger.h"

// -----------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------
#define LOG(fmt, ...) do { TrainerLog::Write(fmt, ##__VA_ARGS__); } while(0)

// -----------------------------------------------------------------------
// Type aliases
// -----------------------------------------------------------------------
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using uptr = uintptr_t;
using iptr = intptr_t;

// -----------------------------------------------------------------------
// Global module handles
// -----------------------------------------------------------------------
inline HMODULE g_hModule     = nullptr;   // WatchDogsTrainer.dll
inline HMODULE g_hGame       = nullptr;   // watch_dogs.exe base
inline HMODULE g_hDisrupt    = nullptr;   // Disrupt_b64.dll base

inline uptr    g_baseGame    = 0;
inline uptr    g_baseDisrupt = 0;
