# package.ps1 — builds the release-dist preset and creates a distributable ZIP.
# Run from the repo root: powershell -ExecutionPolicy Bypass -File scripts\package.ps1

param(
    [string]$Version = "1.0.0"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root    = Split-Path $PSScriptRoot -Parent
$BinDir  = Join-Path $Root "build\release-dist"
$DistDir = Join-Path $Root "dist\solar-cooker-rt"
$ZipOut  = Join-Path $Root "dist\solar-cooker-rt-v$Version-win64.zip"

# ---- 1. Build ---------------------------------------------------------------
Write-Host "Building release-dist..." -ForegroundColor Cyan
cmake --build --preset release-dist --target scrt_app scrt_compare
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# ---- 2. Assemble dist folder ------------------------------------------------
Write-Host "Assembling dist folder..." -ForegroundColor Cyan
if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
New-Item $DistDir -ItemType Directory | Out-Null

# Executables
Copy-Item "$BinDir\scrt_app.exe"     $DistDir
Copy-Item "$BinDir\scrt_compare.exe" $DistDir

# Example scenes
$ExDst = Join-Path $DistDir "examples"
New-Item $ExDst -ItemType Directory | Out-Null
Get-ChildItem "$Root\examples\*.json" | Copy-Item -Destination $ExDst

# ---- 3. Write README --------------------------------------------------------
$Readme = @"
Solar Cooker Ray Tracer  v$Version
====================================
Windows 10/11 x64  —  no installation required, no runtime dependencies.

CONTENTS
--------
  scrt_app.exe       Interactive 3-D viewer (GPU/OpenGL required)
  scrt_compare.exe   Headless batch tool — runs scenes and writes a CSV
  examples\          Six ready-to-use scene files

QUICK START — Interactive viewer
---------------------------------
  scrt_app.exe examples\parabolic_dish.json

  Window controls
    Left-drag     Rotate view
    Right-drag    Pan
    Scroll        Zoom
    Q / Esc       Quit

  GUI panels (left side of window)
    Scene Browser  Select and load any example scene without restarting
    Transforms     Drag sliders to move / rotate any reflector live
    Scene          Edit material reflectance and absorption
    Sun            Adjust DNI (irradiance)
    Trace          Set ray count, click Preview or Full Trace
    Results        Power, peak flux, concentration ratio, wall time

QUICK START — Headless batch compare
--------------------------------------
  Run all example scenes at 500 000 rays, DNI 1000 W/m², save CSV:

    scrt_compare.exe examples\parabolic_dish.json ^
                     examples\parabolic_trough.json ^
                     examples\box_cooker.json ^
                     examples\scheffler_reflector.json ^
                     examples\fresnel_lens_cooker.json ^
                     examples\panel_cooker.json ^
                     --rays 500000 --out results.csv

  Override DNI:
    scrt_compare.exe examples\parabolic_dish.json --dni 850 --out out.csv

  Output columns: scene, total_power_w, peak_flux_wm2, concentration_ratio, wall_time_s

WRITING YOUR OWN SCENE
------------------------
  Copy an examples\ JSON and edit it.  Supported surface types:
    plane                half_width, half_height
    paraboloid           focal_length_m, aperture_radius_m
    cylindrical_paraboloid  focal_length_m, aperture_half_width_m, aperture_half_length_m
    sphere               radius
    quadric              coeffs {A..J}, aperture_box {min, max}
    fresnel_zone_lens    focal_length_m, inner_radius_m, pitch_m, n_zones, n_lens

  Supported material types:
    perfect_mirror
    real_mirror          reflectance (0-1), slope_error_mrad
    absorber             absorption (0-1)
    dielectric           n (refractive index), absorption_m (1/m)

SYSTEM REQUIREMENTS
--------------------
  OS      Windows 10 / 11 (x64)
  GPU     Any DirectX 11 capable card (Intel HD 4000+, NVIDIA Kepler+, AMD GCN+)
  RAM     512 MB free (large scenes with 1M+ rays may use ~200 MB)
  CPU     Any x64 processor (multi-core recommended for 500k+ ray runs)

  scrt_compare.exe is fully headless and works on server / remote machines
  without a GPU or display.
"@
$Readme | Set-Content (Join-Path $DistDir "README.txt") -Encoding UTF8

# ---- 4. Zip -----------------------------------------------------------------
Write-Host "Creating ZIP..." -ForegroundColor Cyan
if (Test-Path $ZipOut) { Remove-Item $ZipOut -Force }
Compress-Archive -Path $DistDir -DestinationPath $ZipOut

$ZipSizeMB = [math]::Round((Get-Item $ZipOut).Length / 1MB, 1)
Write-Host ""
Write-Host "Done -> $ZipOut  ($ZipSizeMB MB)" -ForegroundColor Green
Write-Host ""
Write-Host "Contents:"
Get-ChildItem $DistDir -Recurse | Select-Object -ExpandProperty FullName |
    ForEach-Object { "  " + $_.Replace($DistDir, "solar-cooker-rt") }
