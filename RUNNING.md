# Running the Solar Cooker Ray Tracer

All commands run from the project root: `C:\dev\solar-cooker-rt\`

---

## Build

Configure and compile (first time, or after source changes):

```powershell
cmake --preset debug
cmake --build --preset debug
```

Binaries land in `build/debug/`.  Use `--preset release` for a faster optimised build.

> **Important — always pass `--preset debug` to the build step too.**
> `CMakePresets.json` embeds the `INCLUDE`, `LIB`, and `PATH` variables that MSVC
> needs to find its standard-library headers (e.g. `<cmath>`).  Those variables only
> apply when you use `cmake --build --preset debug`.  Running the bare
> `cmake --build build/debug` bypasses the preset environment and causes a fatal
> `C1083: Cannot open include file: 'cmath'` error.

Run the test suite:

```powershell
ctest --test-dir build/debug --output-on-failure
# or directly:
.\build\debug\scrt_tests.exe
```

---

## scrt_app — interactive 3D viewer

Opens a Polyscope GUI showing the scene geometry, traced rays and the flux heatmap on the receiver.

### Switching scenes without restarting

The GUI has a **Scene Browser** panel (top of the left sidebar).  Pass `--examples-dir` to populate it with any folder of JSON files, then click a name and hit **"Load selected scene"** to swap in-place.

```powershell
# Browse all 49 compact optimisation scenes in-app
.\build\debug\scrt_app.exe results\compact_study\scenes\A_r020.json `
    --examples-dir results\compact_study\scenes

# Browse the standard examples
.\build\debug\scrt_app.exe examples\panel_cooker_12.json `
    --examples-dir examples
```

Loading triggers a 10 k-ray preview automatically.  Hit **Full Trace** in the Trace panel for the full run.

### All flags

```powershell
# Open a specific scene
.\build\debug\scrt_app.exe examples\panel_cooker_12.json

# Open any of the compact optimisation scenes (49 available)
.\build\debug\scrt_app.exe results\compact_study\scenes\A_r020.json
.\build\debug\scrt_app.exe results\compact_study\scenes\E_rw005.json

# Override ray count (GUI still opens with the new count)
.\build\debug\scrt_app.exe examples\panel_cooker_12.json --rays 500000

# Headless — no window, writes flux.csv + summary.json to a folder
.\build\debug\scrt_app.exe examples\panel_cooker_12.json --headless --out results\
.\build\debug\scrt_app.exe examples\panel_cooker_12.json --headless --rays 1000000 --out results\
```

**Flags**

| Flag | Meaning |
|------|---------|
| `--headless` | Skip GUI, trace immediately and write files |
| `--rays N` | Override `n_primary_rays` from the JSON |
| `--out dir/` | Output folder for `flux.csv` and `summary.json` (headless only) |
| `--examples-dir dir/` | Folder scrt_app scans for JSON files to list in the GUI scene-picker |

**Tip:** if no scene path is given, the app looks for an `examples/` folder next to the executable.  That symlink/copy does not exist in the repo, so always pass the path explicitly.

---

## scrt_compare — headless batch comparison

Traces multiple scenes and writes one summary CSV row per scene.

```powershell
# Compare two scenes
.\build\debug\scrt_compare.exe examples\panel_cooker_12.json examples\parabolic_dish.json `
    --rays 200000 --dni 1000 --out results\comparison.csv

# Compare the full examples folder
.\build\debug\scrt_compare.exe examples\*.json --rays 100000 --out results\all.csv

# Compare the compact optimisation scenes
.\build\debug\scrt_compare.exe results\compact_study\scenes\*.json `
    --rays 200000 --out results\compact_summary.csv
```

**Flags**

| Flag | Meaning |
|------|---------|
| `--rays N` | Override ray count for all scenes |
| `--dni D` | Override DNI (W/m²) for all scenes |
| `--out path.csv` | Output CSV path (default: `summary.csv`) |

**Output columns:** `scene, total_power_w, peak_flux_wm2, concentration_ratio, wall_time_s`

---

## Python scripts

Both scripts auto-detect the project root and must be run with Python 3.11+.  No special CWD is required.

### scripts/sweep_stl_rect_angles.py - STL angle and flap-extension study

This is the current workflow for the rectangular STL reflector study. It compares the baseline
STL panel angle scenes, then can generate attached flap-extension candidates for Polyscope
inspection and flux-map comparison.

The design-sweep mode keeps the baseline STL reflector meshes and adds four analytic flap
extensions:

- Each flap is `0.30 m` wide, matching the baseline panel width.
- Each flap extends `0.20 m` from the outer/top edge of the baseline panel.
- Flap angles are swept above the base angle: `base + 5 deg`, `base + 10 deg`, `base + 15 deg`.
- Baseline angles tested by default are `55 deg`, `60 deg`, and `65 deg`.
- Candidates are ranked by total captured power, with center-starved flux maps flagged when
  `center/mean < 75%`.

Run a 1M-ray screening pass:

```powershell
python scripts\sweep_stl_rect_angles.py --design-sweep --screen-only --design-screen-rays 1000000
```

The main outputs are:

- `results/stl_rect_design_sweep/scenes/*.json` - generated candidate scenes
- `results/stl_rect_design_sweep/screen_comparison.txt` - readable ranked summary
- `results/stl_rect_design_sweep/screen_comparison.csv` - raw comparison table
- `results/stl_rect_design_sweep/screen_maps/*.png` - shared-scale flux maps

Run the three-panel version, which chains a second flap onto the outer edge of the first added
flap. The final panel is the same `0.30 m x 0.20 m` size and always uses an angle larger than
the preceding flap:

```powershell
python scripts\sweep_stl_rect_angles.py --design-3-panel-sweep --screen-only --design-screen-rays 1000000
```

The three-panel sweep writes to a separate folder so the two-panel results are not touched:

- `results/stl_rect_design_3_panels_sweep/scenes/*.json`
- `results/stl_rect_design_3_panels_sweep/screen_comparison.txt`
- `results/stl_rect_design_3_panels_sweep/screen_comparison.csv`
- `results/stl_rect_design_3_panels_sweep/screen_maps/*.png`

Open the generated candidates in Polyscope:

```powershell
.\build\debug\scrt_app.exe --examples-dir .\results\stl_rect_design_sweep\scenes
```

For the three-panel sweep, point Polyscope at the separate generated-scene folder:

```powershell
.\build\debug\scrt_app.exe --examples-dir .\results\stl_rect_design_3_panels_sweep\scenes
```

The viewer's Scene Browser lists every generated candidate. Load them one at a time to verify
that each flap is hinged to the outer panel edge and tilts radially outward from its parent
panel.

### Box receiver STL runs

Box receiver runs write outside `results/`, under the top-level `box_results/` folder. The
glass top records flux and passes rays through; the bottom and four walls record and absorb.
Each run writes per-face CSVs under `csv/` and rendered maps under `images/`.

Run the three-panel `60/65/70` STL with the indented box receiver:

```powershell
.\build\debug\scrt_app.exe examples\stl_rectangular_3floors_606570deg_box.json --headless --rays 1000000 --out box_results\box_receiver_606570_stl_1m
python scripts\plot_box_flux_maps.py box_results\box_receiver_606570_stl_1m
```

Run the two-panel `60/65` STL with the same box receiver:

```powershell
.\build\debug\scrt_app.exe examples\stl_rectangular_2floors_6065deg_box.json --headless --rays 1000000 --out box_results\box_receiver_6065_stl_1m
python scripts\plot_box_flux_maps.py box_results\box_receiver_6065_stl_1m
```

For 10M-ray finalist runs, use the same commands with `--rays 10000000` and folders such as
`box_results\box_receiver_606570_stl_10m` or `box_results\box_receiver_6065_stl_10m`.

Each run folder contains:

- `summary.json`
- `csv/flux_glass_top.csv`, `csv/flux_bottom.csv`, and one CSV per wall
- `images/unfolded_box_flux.png`
- `images/flux_glass_top.png`, `images/flux_bottom.png`, and one PNG per wall

Run the original fixed-angle STL sweep:

```powershell
python scripts\sweep_stl_rect_angles.py --rays 100000 --flux-rays 10000000
```

That writes:

- `results/stl_rect_angle_sweep/results.csv`
- `results/stl_rect_angle_sweep/summary.txt`
- `results/stl_rect_angle_sweep/flux_comparison.csv`
- `results/stl_rect_angle_sweep/flux_comparison.txt`
- `results/stl_rect_angle_sweep/flux_*.png`

### scripts/sweep.py — multi-parameter sweep over all example scenes

Runs every example scene at four ray counts × four DNI values (16 runs each) and produces three summary tables.

```powershell
python scripts\sweep.py
```

Output files:
- `results/sweep_results.csv` — raw data (scene × rays × DNI)
- `results/sweep_tables.txt` — three formatted tables (power vs rays, power vs DNI, combined stats)

The script also covers the panel-count series (`panel_cooker_6` … `panel_cooker_26`) and the receiver-offset variants (`panel_cooker_12_receiver_*`).

### scripts/optimize_compact.py — compact 12-panel optimisation sweep

Generates scenes constrained to a **0.5 m × 0.5 m** footprint and sweeps five parameter dimensions.  Panel angles are derived analytically from the reflection formula, so all generated scenes are physically correct.

```powershell
python scripts\optimize_compact.py
```

Output files:
- `results/compact_study/scenes/*.json` — 49 generated scene files (loadable in scrt_app)
- `results/compact_study/report.txt` — five result tables + best-design summary

**Studies run:**

| Study | What varies | Key finding |
|-------|-------------|-------------|
| A — Ring radius | r = 0.08 … 0.20 m (h_z = r) | Larger ring → more power; r = 0.20 m best |
| B — Tilt angle | h_z = 0.06 … 0.30 m at r = 0.20 m | 67.5° tilt (h_z = r) is the sweet spot |
| C — Panel size | hw × hh grid 0.04–0.07 × 0.06–0.12 m | 14 cm × 20 cm panels give the most power |
| D — Receiver distance | z_recv = −0.15 … +0.20 m | Receiver at z = 0 (same plane as ring base) |
| E — Receiver size | half-side 0.02 … 0.20 m | 10 cm square → 74 W, 14× CR; 30 cm → 158 W, 10× CR |

---

## Scene JSON format (quick reference)

All scenes live in `examples/` (shipped) or `results/compact_study/scenes/` (generated).  A scene file contains:

```json
{
  "scene": {
    "sun":      { "direction": [0,0,-1], "dni_wm2": 1000, "sunshape": {...} },
    "aperture": { "type": "disk", "center": [...], "normal": [...], "radius": 0.25 },
    "materials": [ { "id": "foil_mirror", "type": "real_mirror", "reflectance": 0.85, "slope_error_mrad": 4.0 } ],
    "elements": [ { "name": "...", "material": "foil_mirror", "surface": {...}, "transform": {...} } ],
    "receiver": { "surface": { "type": "plane", "half_width": 0.05, "half_height": 0.05 },
                  "grid": {"nx": 64, "ny": 64},
                  "transform": { "translation": [0,0,0] } }
  },
  "trace": { "n_primary_rays": 200000, "max_bounces": 3, "rng_seed": 13 }
}
```

Panel rotation uses `rotation_euler_deg: [pitch, yaw, roll]` applied as Rx(pitch)·Ry(yaw)·Rz(roll).  Translations are in metres; angles in degrees.

---

## Viewing a compact scene

```powershell
# Best overall geometry from the optimisation
.\build\debug\scrt_app.exe results\compact_study\scenes\A_r020.json

# Best panel-size variant
.\build\debug\scrt_app.exe results\compact_study\scenes\C_hw07_hh10.json

# Receiver size comparison: tight focus vs wide capture
.\build\debug\scrt_app.exe results\compact_study\scenes\E_rw002.json   # 4x4 cm, CR 30x
.\build\debug\scrt_app.exe results\compact_study\scenes\E_rw015.json   # 30x30 cm, 158 W
```
