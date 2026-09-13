# Launches the app, photographs its window, and closes it again.
#
# Exists because `--headless` does not construct the viewer at all: a startup crash once shipped
# with every headless check green, and no automated check in this repo has ever looked at a pixel
# of the interface. This is the cheapest thing that does.
#
# Captures only the app's own window rectangle, not the whole desktop.
#
#   powershell -ExecutionPolicy Bypass -File scripts/screenshot_app.ps1 `
#       -Exe build/debug/scrt_app.exe -Out build/shot.png -WaitSeconds 14
param(
    [string]$Exe         = "build/debug/scrt_app.exe",
    [string]$Out         = "build/app-screenshot.png",
    [int]   $WaitSeconds = 14,
    [string]$Scene       = "",
    [string]$Clicks      = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    // Without this, a 150%-scaled display reports the window rect in logical pixels and
    // CopyFromScreen hands back a downscaled, blurry image of the wrong size.
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
}
"@

# Per-monitor v2, not just system-DPI: GLFW makes the app per-monitor aware, so on a mixed-DPI
# desktop a merely system-aware observer reads its window rect in the wrong coordinate space and
# photographs the wrong rectangle. -4 is DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
[void][Win]::SetProcessDPIAware()
[void][Win]::SetThreadDpiAwarenessContext([IntPtr](-4))

$exePath = (Resolve-Path $Exe).Path
$args = @("--examples-dir", "examples")
if ($Scene -ne "") { $args = @($Scene) + $args }

Write-Host "launching $exePath"
# Run from the repo root: --examples-dir is relative, and the app resolves a scene's mesh
# paths against the scene file, which lives under examples/.
$root = (Get-Location).Path
$log  = Join-Path $root "build/app-launch.log"
$p = Start-Process -FilePath $exePath -ArgumentList $args -PassThru -WorkingDirectory $root `
                   -RedirectStandardOutput $log -RedirectStandardError "$log.err"

# The window is created some way into startup, and the preview trace runs after that. Waiting
# on the handle rather than a fixed sleep keeps this honest on a slow debug build.
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 400
    $p.Refresh()
    if ($p.HasExited) {
        Write-Host "--- stdout ---"; if (Test-Path $log)     { Get-Content $log     | Select-Object -Last 30 }
        Write-Host "--- stderr ---"; if (Test-Path "$log.err") { Get-Content "$log.err" | Select-Object -Last 30 }
        throw "app exited early with code $($p.ExitCode) - it did not hold a window"
    }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) { break }
}
$p.Refresh()
if ($p.MainWindowHandle -eq [IntPtr]::Zero) {
    $p.Kill(); throw "no window appeared within $WaitSeconds s"
}

# Let the first frames settle: the preview trace repaints, and a shot taken mid-load shows an
# empty scene that looks like a bug.
Start-Sleep -Seconds ([Math]::Max(3, [int]($WaitSeconds / 2)))

$h = $p.MainWindowHandle
# SW_SHOW, never SW_RESTORE: restore un-maximises a maximised window, so the harness was
# photographing a 1302x776 window and reporting the app as "not maximised" when it was.
[void][Win]::ShowWindow($h, 5)
[void][Win]::SetForegroundWindow($h)
# Windows refuses SetForegroundWindow to a process that does not own the foreground, and a
# terminal left sitting over the app then ends up in the photograph. Topmost is not refused.
[void][Win]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0043)   # HWND_TOPMOST, NOMOVE|NOSIZE|SHOWWINDOW
Start-Sleep -Milliseconds 1200

# Re-assert per-monitor awareness on this very thread immediately before the rect query. Set
# once at the top of the script it did not stick, and a system-DPI-aware reader hands back the
# window's pre-maximise geometry on a mixed-DPI desktop.
[void][Win]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$r = New-Object Win+RECT
if (-not [Win]::GetWindowRect($h, [ref]$r)) { $p.Kill(); throw "GetWindowRect failed" }

# Optional clicks, as fractions of the window rect, e.g. "0.06,0.18;0.5,0.5". Fractions rather
# than pixels so a sequence keeps working on a different monitor.
foreach ($c in ($Clicks -split ";" | Where-Object { $_ -ne "" })) {
    $f = $c -split ","
    $cx = [int]($r.L + ($r.R - $r.L) * [double]$f[0])
    $cy = [int]($r.T + ($r.B - $r.T) * [double]$f[1])
    Write-Host "click at $cx,$cy"
    [void][Win]::SetCursorPos($cx, $cy)
    Start-Sleep -Milliseconds 250
    [Win]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # LEFTDOWN
    Start-Sleep -Milliseconds 80
    [Win]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # LEFTUP
    Start-Sleep -Milliseconds 700
}
Write-Host "window rect: L=$($r.L) T=$($r.T) R=$($r.R) B=$($r.B)"
$w = $r.R - $r.L; $ht = $r.B - $r.T
if ($w -le 0 -or $ht -le 0) { $p.Kill(); throw "window rect is empty ($w x $ht)" }

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g   = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $ht))
$g.Dispose()

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }
$bmp.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

[void][Win]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0043)   # HWND_NOTOPMOST
$p.CloseMainWindow() | Out-Null
Start-Sleep -Seconds 2
if (-not $p.HasExited) { $p.Kill() }

Write-Host "wrote $Out  ($w x $ht)"
