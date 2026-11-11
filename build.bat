@echo off
rem Build (Release) and optionally run:  build.bat            -> build only
rem                                      build.bat run [args] -> build, then render
setlocal
cd /d "%~dp0"
cmake -S . -B build || exit /b 1
cmake --build build --config Release --parallel || exit /b 1
set EXE=build\Release\octree_render.exe
if not exist %EXE% set EXE=build\octree_render.exe
echo Built: %EXE%
if /i not "%~1"=="run" exit /b 0
shift
set ARGS=
:collect
if "%~1"=="" goto launch
set ARGS=%ARGS% %1
shift
goto collect
:launch
%EXE% %ARGS%
