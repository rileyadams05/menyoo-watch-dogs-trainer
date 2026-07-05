@echo off
setlocal
echo ================================================
echo  Watch Dogs Trainer - Dependency Setup
echo ================================================

set EXTERN=%~dp0..\extern

if not exist "%EXTERN%" mkdir "%EXTERN%"

REM --- Dear ImGui ---
if not exist "%EXTERN%\imgui\imgui.h" (
    echo [*] Cloning Dear ImGui...
    git clone --depth=1 --branch docking https://github.com/ocornut/imgui.git "%EXTERN%\imgui"
) else (
    echo [+] ImGui already present.
)

REM --- MinHook ---
if not exist "%EXTERN%\minhook\include\MinHook.h" (
    echo [*] Cloning MinHook...
    git clone --depth=1 https://github.com/TsudaKageyu/minhook.git "%EXTERN%\minhook"
) else (
    echo [+] MinHook already present.
)

REM --- nlohmann/json ---
if not exist "%EXTERN%\json\include\nlohmann\json.hpp" (
    echo [*] Cloning nlohmann/json...
    git clone --depth=1 https://github.com/nlohmann/json.git "%EXTERN%\json"
) else (
    echo [+] JSON already present.
)

echo.
echo [+] All dependencies ready.
echo.
echo Run build.bat to compile.
endlocal
