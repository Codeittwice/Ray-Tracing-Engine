@echo off
REM Build and launch the app straight from the repo - no packaging, no zip.
REM Double-click this file, or pass a scene:
REM     run-release.bat examples\panel_cooker_compact_best.json
REM Optimised build: slower to compile, far faster to trace. Use for big ray counts.
setlocal
cd /d "%~dp0"

set "SCENE=%~1"
if "%SCENE%"=="" set "SCENE=examples\panel_cooker_compact_best.json"

echo Building (release)...
cmake --build --preset release --target scrt_app
if errorlevel 1 (
  echo.
  echo Build failed - not launching.
  pause
  exit /b 1
)

echo Launching %SCENE%
"buildelease\scrt_app.exe" "%SCENE%" --examples-dir examples
if errorlevel 1 pause
