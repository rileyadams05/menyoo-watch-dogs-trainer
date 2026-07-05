cmake_minimum_required(VERSION 3.20)
project(WatchDogsTrainer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Must be built as 64-bit (x64)")
endif()

# -----------------------------------------------------------------------
# ImGui
# -----------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

# -----------------------------------------------------------------------
# MinHook
# -----------------------------------------------------------------------
set(MINHOOK_DIR ${CMAKE_SOURCE_DIR}/extern/minhook)
set(MINHOOK_SOURCES
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

# -----------------------------------------------------------------------
# Trainer DLL
# -----------------------------------------------------------------------
add_library(WatchDogsTrainer SHARED
    src/dllmain.cpp
    src/diagnostics/diagnostics.cpp
    src/memory/memory.cpp
    src/memory/pattern_scan.cpp
    src/memory/pointer_chain.cpp
    src/process/process.cpp
    src/logging/logger.cpp
    src/hooks/hooks.cpp
    src/hooks/dx11_hook.cpp
    src/cheats/cheat_manager.cpp
    src/cheats/godmode.cpp
    src/cheats/focus.cpp
    src/cheats/battery.cpp
    src/cheats/ammo.cpp
    src/cheats/crafting.cpp
    src/cheats/skills.cpp
    src/cheats/money.cpp
    src/cheats/wanted.cpp
    src/cheats/stealth.cpp
    src/cheats/hacking.cpp
    src/cheats/vehicles.cpp
    src/cheats/noclip.cpp
    src/cheats/teleport.cpp
    src/cheats/time_of_day.cpp
    src/cheats/fov.cpp
    src/cheats/digital_trips.cpp
    src/cheats/misc.cpp
    src/ui/ui.cpp
    src/ui/menu.cpp
    src/config/config.cpp
    ${IMGUI_SOURCES}
    ${MINHOOK_SOURCES}
)

target_include_directories(WatchDogsTrainer PRIVATE
    src
    src/diagnostics
    src/logging
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${MINHOOK_DIR}/include
    ${CMAKE_SOURCE_DIR}/extern/json/include
)

target_compile_definitions(WatchDogsTrainer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

target_link_libraries(WatchDogsTrainer PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
)

set_target_properties(WatchDogsTrainer PROPERTIES
    OUTPUT_NAME "WatchDogsTrainer"
    SUFFIX ".dll"
)

# -----------------------------------------------------------------------
# Loader EXE
# -----------------------------------------------------------------------
add_executable(WatchDogsLoader WIN32
    loader/main.cpp
    loader/injector.cpp
    loader/WatchDogsLoader.rc
)

target_include_directories(WatchDogsLoader PRIVATE
    loader
    ${CMAKE_SOURCE_DIR}/extern/json/include
)

target_compile_definitions(WatchDogsLoader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

target_link_libraries(WatchDogsLoader PRIVATE
    advapi32.lib
    comdlg32.lib
    dwmapi.lib
    gdiplus.lib
    psapi.lib
    shell32.lib
)

set_target_properties(WatchDogsLoader PROPERTIES
    OUTPUT_NAME "Watch-Dogs-MENYOO"
)
