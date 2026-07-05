@echo off
setlocal
set "VSINSTALLDIR=G:\Microsoft Visual Studio\Community\"
set "VCINSTALLDIR=%VSINSTALLDIR%VC\"
call "%VCINSTALLDIR%Auxiliary\Build\vcvars64.bat"
pushd "%~dp0.."
cmake --build build --config Release
popd
