#!/usr/bin/env python3
"""
Multi-test sweep: ray count x DNI x all scenes.
Runs scrt_compare for each (rays, dni) combination, collects power / CR,
computes optical efficiency, then prints three summary tables and saves
full raw data to results/sweep_results.csv.
"""

import csv
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRT_COMPARE = Path("build/debug/scrt_compare.exe")
TEMP_CSV     = Path("results/sweep_temp.csv")
OUT_CSV      = Path("results/sweep_results.csv")
OUT_TXT      = Path("results/sweep_tables.txt")

SCENES = [
    "examples/parabolic_dish.json",
    "examples/parabolic_trough.json",
    "examples/box_cooker.json",
    "examples/scheffler_reflector.json",
    "examples/fresnel_lens_cooker.json",
    "examples/panel_cooker.json",
]

RAY_COUNTS = [10_000, 50_000, 200_000, 500_000]
DNI_VALUES = [700, 850, 1000, 1100]   # W/m²

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def aperture_area(scene_path: str) -> float:
    with open(scene_path) as f:
        j = json.load(f)
    r = j["scene"]["aperture"]["radius"]
    return math.pi * r * r


def run_compare(scenes, rays, dni):
    cmd = [str(SCRT_COMPARE)] + scenes + [
        "--rays", str(rays),
        "--dni",  str(dni),
        "--out",  str(TEMP_CSV),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"  WARN: non-zero exit for rays={rays} dni={dni}", file=sys.stderr)
        return None
    rows = {}
    with open(TEMP_CSV, newline="") as f:
        for row in csv.DictReader(f):
            name = Path(row["scene"]).stem
            rows[name] = {
                "power": float(row["total_power_w"]),
                "cr":    float(row["concentration_ratio"]),
            }
    return rows


def fmt_col(v, width=10, decimals=1):
    return f"{v:>{width}.{decimals}f}"


def hline(width):
    return "-" * width

# ---------------------------------------------------------------------------
# Main sweep
# ---------------------------------------------------------------------------

scene_names  = [Path(s).stem for s in SCENES]
areas        = {Path(s).stem: aperture_area(s) for s in SCENES}
all_results  = {n: [] for n in scene_names}   # name -> [(rays,dni,power,cr,eta)]

total_runs = len(RAY_COUNTS) * len(DNI_VALUES)
run = 0
for rays in RAY_COUNTS:
    for dni in DNI_VALUES:
        run += 1
        print(f"[{run:2d}/{total_runs}]  rays={rays:>8,}   dni={dni:4d} W/m² ... ", end="", flush=True)
        rows = run_compare(SCENES, rays, dni)
        if rows is None:
            print("FAILED")
            continue
        for name in scene_names:
            if name in rows:
                power = rows[name]["power"]
                cr    = rows[name]["cr"]
                eta   = power / (dni * areas[name]) * 100.0
                all_results[name].append((rays, dni, power, cr, eta))
        print("ok")

TEMP_CSV.unlink(missing_ok=True)

# ---------------------------------------------------------------------------
# Save raw CSV
# ---------------------------------------------------------------------------

OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
with open(OUT_CSV, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["scene", "rays", "dni_wm2", "power_w", "cr", "efficiency_pct"])
    for name in scene_names:
        for rays, dni, power, cr, eta in all_results[name]:
            w.writerow([name, rays, dni, f"{power:.4f}", f"{cr:.6f}", f"{eta:.4f}"])
print(f"\nRaw data -> {OUT_CSV}")

# ---------------------------------------------------------------------------
# Tables
# ---------------------------------------------------------------------------

lines = []

def pr(*args, **kw):
    s = " ".join(str(a) for a in args)
    lines.append(s)
    print(s, **kw)

# --- Table 1: Ray count sweep at DNI = 1000 ---------------------------------

REF_DNI = 1000
pr()
pr(f"## Table 1 — Power (W) across ray counts  [DNI = {REF_DNI} W/m²]")
pr()
col_w = 11
header = f"{'Scene':<28}" + "".join(f"{r:>{col_w},}" for r in RAY_COUNTS) + \
         f"{'Mean W':>{col_w}}{'Std W':>{col_w}}{'eta mean%':>{col_w}}{'eta std%':>{col_w}}"
pr(header)
pr(hline(len(header)))
for name in scene_names:
    runs = [(r, p, e) for r, d, p, _, e in all_results[name] if d == REF_DNI]
    if not runs:
        continue
    powers = [p for _, p, _ in runs]
    etas   = [e for _, _, e in runs]
    row    = f"{name:<28}"
    row   += "".join(f"{p:>{col_w}.1f}" for _, p, _ in runs)
    row   += f"{statistics.mean(powers):>{col_w}.1f}"
    row   += f"{statistics.stdev(powers) if len(powers) > 1 else 0.0:>{col_w}.2f}"
    row   += f"{statistics.mean(etas):>{col_w}.2f}"
    row   += f"{statistics.stdev(etas) if len(etas) > 1 else 0.0:>{col_w}.2f}"
    pr(row)

# --- Table 2: DNI sweep at highest ray count --------------------------------

REF_RAYS = max(RAY_COUNTS)
pr()
pr(f"## Table 2 — Power (W) across DNI values  [rays = {REF_RAYS:,}]")
pr()
col_w2 = 12
header2 = f"{'Scene':<28}" + "".join(f"{d:>{col_w2}} W/m²" for d in DNI_VALUES) + \
          f"{'eta mean%':>{col_w2}}{'eta std%':>{col_w2}}"
pr(header2)
pr(hline(len(header2)))
for name in scene_names:
    runs = [(d, p, e) for r, d, p, _, e in all_results[name] if r == REF_RAYS]
    if not runs:
        continue
    etas = [e for _, _, e in runs]
    row  = f"{name:<28}"
    row += "".join(f"{p:>{col_w2}.1f}" for _, p, _ in runs)
    row += f"{statistics.mean(etas):>{col_w2}.2f}"
    row += f"{statistics.stdev(etas) if len(etas) > 1 else 0.0:>{col_w2}.2f}"
    pr(row)

# --- Table 3: Combined stats across all tests --------------------------------

pr()
pr(f"## Table 3 — Combined stats across all {total_runs} (rays × DNI) tests")
pr()
col_w3 = 11
header3 = f"{'Scene':<28}" + \
          f"{'Mean W':>{col_w3}}{'Std W':>{col_w3}}" + \
          f"{'eta mean%':>{col_w3}}{'eta std%':>{col_w3}}" + \
          f"{'eta min%':>{col_w3}}{'eta max%':>{col_w3}}"
pr(header3)
pr(hline(len(header3)))
for name in scene_names:
    if not all_results[name]:
        continue
    powers = [p for _, _, p, _, _ in all_results[name]]
    etas   = [e for _, _, _, _, e in all_results[name]]
    row    = f"{name:<28}"
    row   += f"{statistics.mean(powers):>{col_w3}.1f}"
    row   += f"{statistics.stdev(powers) if len(powers) > 1 else 0.0:>{col_w3}.2f}"
    row   += f"{statistics.mean(etas):>{col_w3}.2f}"
    row   += f"{statistics.stdev(etas) if len(etas) > 1 else 0.0:>{col_w3}.2f}"
    row   += f"{min(etas):>{col_w3}.2f}"
    row   += f"{max(etas):>{col_w3}.2f}"
    pr(row)

pr()
pr("Notes:")
pr("  eta (optical efficiency) = captured_power / (DNI x aperture_area) x 100 %")
pr("  Std W in Table 1 reflects Monte Carlo noise across ray counts.")
pr("  eta std% in Table 3 combines both ray-count noise and DNI scaling.")

# Save tables text
with open(OUT_TXT, "w") as f:
    f.write("\n".join(lines) + "\n")
print(f"Tables    -> {OUT_TXT}")
