@echo off
setlocal
set "VSINSTALLDIR=C:\Program Files\Microsoft Visual Studio\18\Community\"
set "VCINSTALLDIR=%VSINSTALLDIR%VC\"
call "%VCINSTALLDIR%Auxiliary\Build\vcvars64.bat"
pushd "%~dp0.."
echo After pushd, CD=%CD%
cmake -S . -B build -G "NMake Makefiles"
if errorlevel 1 goto :eof
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
popd
