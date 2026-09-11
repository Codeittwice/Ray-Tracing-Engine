@echo off
REM Build and launch the app straight from the repo - no packaging, no zip.
REM Double-click this file, or pass a scene:
REM     run.bat examples\panel_cooker_compact_best.json
REM Debug build: quick to compile, slower to trace. Good for UI work.
setlocal
cd /d "%~dp0"

set "SCENE=%~1"
if "%SCENE%"=="" set "SCENE=examples\panel_cooker_compact_best.json"

echo Building (debug)...
cmake --build --preset debug --target scrt_app
if errorlevel 1 (
  echo.
  echo Build failed - not launching.
  pause
  exit /b 1
)

echo Launching %SCENE%
"build\debug\scrt_app.exe" "%SCENE%" --examples-dir examples
if errorlevel 1 pause
