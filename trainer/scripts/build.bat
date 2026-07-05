@echo off
setlocal
echo ================================================
echo  Watch Dogs Trainer - Build
echo ================================================

set ROOT=%~dp0..
set BUILD=%ROOT%\build

if not exist "%BUILD%" mkdir "%BUILD%"

cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [!] CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD%" --config Release --parallel
if errorlevel 1 (
    echo [!] Build failed.
    exit /b 1
)

echo.
echo [+] Build complete.
echo     DLL:    %BUILD%\Release\WatchDogsTrainer.dll
echo     Loader: %BUILD%\Release\Watch-Dogs-MENYOO.exe
echo.
echo Copy both to the same folder and run Watch-Dogs-MENYOO.exe before or after starting Watch Dogs.
endlocal
