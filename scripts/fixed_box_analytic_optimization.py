#!/usr/bin/env python3
"""Generate and run fixed-width analytic box optimization studies."""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from sweep_stl_rect_angles import (
    SCRT_APP,
    make_analytic_size_scene,
    run_headless_flux,
    write_json,
)


ROOT = Path(__file__).resolve().parents[1]
OUT_ROOT = ROOT / "results" / "fixed_box_analytic_optimization"
PLOT_BOX = ROOT / "scripts" / "plot_box_flux_maps.py"
BOX_HALF_WIDTH_M = 0.15
BOX_HALF_HEIGHT_M = 0.15
DEFAULT_DEPTH_M = 0.15
DEFAULT_TOP_Z_M = -0.1324038
DEFAULT_RAYS = 1_000_000
DEFAULT_FINALIST_RAYS = 10_000_000
DEFAULT_SEED = 42
DEFAULT_DNI = 1000.0
CENTER_MEAN_WARN_PCT = 75.0
INTERIOR_FINISH = "matt black absorber"

ROUTES = [
    ("01_reflector_angle_size_sweep", "Reflector Angle And Size Sweep"),
    ("02_box_depth_receiver_position_sweep", "Box Depth And Battery Position Sweep"),
    ("03_battery_absorber_coupling_sweep", "Matt-Black Box And Battery Coupling"),
    ("04_greenhouse_optics_sensitivity", "Greenhouse Optics Sensitivity"),
    ("05_robustness_sun_error_sweep", "Robustness And Real-Use Errors"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fixed 0.30 m box analytic optimization study workflow."
    )
    parser.add_argument("--out-root", type=Path, default=OUT_ROOT)
    parser.add_argument("--screen-rays", type=int, default=DEFAULT_RAYS)
    parser.add_argument("--finalist-rays", type=int, default=DEFAULT_FINALIST_RAYS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--dni", type=float, default=DEFAULT_DNI)
    parser.add_argument(
        "--run-screen",
        action="store_true",
        help="Run every generated screening scene. Without this, only scenes and reports are written.",
    )
    parser.add_argument(
        "--run-finalists",
        action="store_true",
        help="Run 10M-ray finalists from existing screen tables.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove the study folder before generating outputs.",
    )
    parser.add_argument(
        "--limit-route1",
        type=int,
        default=0,
        help="Optional cap for route 1 screening candidates, useful for smoke tests.",
    )
    parser.add_argument(
        "--run-follow-up",
        action="store_true",
        help="Run the focused higher-angle follow-up sweep and 10M-ray finalist confirmations.",
    )
    return parser.parse_args()


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT).as_posix()


def now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def ensure_route_dirs(out_root: Path) -> dict[str, Path]:
    out_root.mkdir(parents=True, exist_ok=True)
    route_dirs: dict[str, Path] = {}
    for route_slug, _ in ROUTES:
        route = out_root / route_slug
        route_dirs[route_slug] = route
        for child in ["scenes", "runs", "maps", "tables"]:
            (route / child).mkdir(parents=True, exist_ok=True)
    return route_dirs


def base_trace(root: dict, rays: int, seed: int) -> None:
    root.setdefault("trace", {})
    root["trace"]["n_primary_rays"] = rays
    root["trace"]["rng_seed"] = seed
    root["trace"]["record_paths"] = False
    root["trace"]["max_paths_to_record"] = 500
    root["trace"]["max_bounces"] = max(8, int(root["trace"].get("max_bounces", 8)))


def set_box_receiver(
    root: dict,
    depth_m: float = DEFAULT_DEPTH_M,
    battery_top_depth_m: float | None = None,
    battery_half_width_m: float = 0.075,
    battery_half_height_m: float = 0.075,
) -> None:
    receiver: dict = {
        "type": "box",
        "top_mode": "record_pass",
        "depth": round(depth_m, 6),
        "surface": {
            "type": "plane",
            "half_width": BOX_HALF_WIDTH_M,
            "half_height": BOX_HALF_HEIGHT_M,
        },
        "grid": {"nx": 64, "ny": 64},
        "transform": {"translation": [0.0, 0.0, DEFAULT_TOP_Z_M]},
    }
    if battery_top_depth_m is not None:
        receiver["battery"] = {
            "enabled": True,
            "top_depth_m": round(battery_top_depth_m, 6),
            "half_width": round(battery_half_width_m, 6),
            "half_height": round(battery_half_height_m, 6),
            "nx": 48,
            "ny": 48,
        }
    root["scene"]["receiver"] = receiver


def set_material(root: dict, reflectance: float = 0.85, slope_error_mrad: float = 2.0) -> None:
    root["scene"]["materials"] = [
        {
            "id": "aluminum",
            "type": "real_mirror",
            "reflectance": round(reflectance, 6),
            "slope_error_mrad": round(slope_error_mrad, 6),
        }
    ]


def set_sun(
    root: dict,
    dni: float,
    sunshape: str = "pillbox",
    yaw_deg: float = 0.0,
    pitch_deg: float = 0.0,
) -> None:
    yaw = math.radians(yaw_deg)
    pitch = math.radians(pitch_deg)
    direction = [
        round(math.sin(yaw) * math.cos(pitch), 9),
        round(math.sin(pitch), 9),
        round(-math.cos(yaw) * math.cos(pitch), 9),
    ]
    sun = {
        "direction": direction,
        "dni_wm2": round(dni, 6),
        "sunshape": {"type": sunshape},
    }
    if sunshape == "pillbox":
        sun["sunshape"]["half_angle_mrad"] = 4.65
    elif sunshape == "buie":
        sun["sunshape"]["chi"] = 0.05
    else:
        raise ValueError(f"unsupported sunshape: {sunshape}")
    root["scene"]["sun"] = sun


def fixed_width_scene(
    label: str,
    angles: tuple[int, int, int],
    heights: tuple[float, float, float],
    rays: int,
    seed: int,
    dni: float,
    depth_m: float = DEFAULT_DEPTH_M,
    battery_top_depth_m: float | None = None,
    battery_half_width_m: float = 0.075,
    battery_half_height_m: float = 0.075,
    reflectance: float = 0.85,
    slope_error_mrad: float = 2.0,
    sunshape: str = "pillbox",
    yaw_deg: float = 0.0,
    pitch_deg: float = 0.0,
) -> dict:
    root = make_analytic_size_scene(label, angles, 0.30, heights)
    root["scene"]["name"] = label
    set_material(root, reflectance=reflectance, slope_error_mrad=slope_error_mrad)
    set_sun(root, dni=dni, sunshape=sunshape, yaw_deg=yaw_deg, pitch_deg=pitch_deg)
    set_box_receiver(
        root,
        depth_m=depth_m,
        battery_top_depth_m=battery_top_depth_m,
        battery_half_width_m=battery_half_width_m,
        battery_half_height_m=battery_half_height_m,
    )
    base_trace(root, rays, seed)
    return root


def route1_cases() -> list[dict]:
    cases = []
    angle_sets = itertools.product(range(58, 63), range(63, 68), range(68, 73))
    height_sets = [
        (0.30, 0.20, 0.20),
        (0.30, 0.25, 0.20),
        (0.25, 0.25, 0.25),
        (0.25, 0.20, 0.25),
    ]
    for angles, heights in itertools.product(angle_sets, height_sets):
        label = (
            f"r1_a{angles[0]}_{angles[1]}_{angles[2]}"
            f"_h{int(heights[0] * 100):02d}_{int(heights[1] * 100):02d}_{int(heights[2] * 100):02d}"
        )
        cases.append({"route": "01", "label": label, "angles": angles, "heights": heights})
    return cases


def route2_cases(seed_case: dict) -> list[dict]:
    cases = []
    for depth in [0.10, 0.125, 0.15, 0.175, 0.20]:
        for lift in [0.0, 0.025, 0.05, 0.075]:
            top_depth = None if lift == 0.0 else max(0.005, depth - lift)
            label = f"r2_depth{int(depth * 1000):03d}_battery_lift{int(lift * 1000):03d}"
            cases.append({
                **seed_case,
                "route": "02",
                "label": label,
                "depth_m": depth,
                "battery_lift_m": lift,
                "battery_top_depth_m": top_depth,
            })
    return cases


def route3_cases(seed_case: dict) -> list[dict]:
    variants = [
        ("battery_bottom_full", None, 0.15, 0.15),
        ("battery_raised_25mm_nominal", DEFAULT_DEPTH_M - 0.025, 0.075, 0.075),
        ("battery_raised_50mm_nominal", DEFAULT_DEPTH_M - 0.050, 0.075, 0.075),
        ("battery_raised_50mm_wide", DEFAULT_DEPTH_M - 0.050, 0.100, 0.075),
        ("battery_raised_50mm_narrow", DEFAULT_DEPTH_M - 0.050, 0.060, 0.075),
    ]
    cases = []
    for name, top_depth, hw, hh in variants:
        cases.append({
            **seed_case,
            "route": "03",
            "label": f"r3_{name}",
            "battery_top_depth_m": top_depth,
            "battery_half_width_m": hw,
            "battery_half_height_m": hh,
        })
    return cases


def route4_cases(seed_case: dict) -> list[dict]:
    cases = []
    for tx in [0.85, 0.90, 0.95]:
        cases.append({
            **seed_case,
            "route": "04",
            "label": f"r4_glass_tx_{int(tx * 100):02d}",
            "glass_transmission": tx,
        })
    return cases


def route5_cases(seed_case: dict) -> list[dict]:
    cases = []
    for yaw, pitch in [(-10, 0), (-5, 0), (0, 0), (5, 0), (10, 0), (0, -5), (0, 5)]:
        cases.append({**seed_case, "route": "05", "label": f"r5_sun_yaw{yaw:+03d}_pitch{pitch:+03d}", "yaw_deg": yaw, "pitch_deg": pitch})
    for sunshape in ["pillbox", "buie"]:
        cases.append({**seed_case, "route": "05", "label": f"r5_sunshape_{sunshape}", "sunshape": sunshape})
    for reflectance in [0.75, 0.85, 0.92]:
        cases.append({**seed_case, "route": "05", "label": f"r5_reflectance_{int(reflectance * 100):02d}", "reflectance": reflectance})
    for slope_error in [0.0, 2.0, 5.0, 10.0]:
        cases.append({**seed_case, "route": "05", "label": f"r5_slope_error_{int(slope_error):02d}mrad", "slope_error_mrad": slope_error})
    for delta in [-3, -2, -1, 1, 2, 3]:
        angles = tuple(int(a + delta) for a in seed_case["angles"])
        cases.append({**seed_case, "route": "05", "label": f"r5_angle_error_{delta:+02d}deg", "angles": angles})
    return cases


def follow_up_cases() -> list[dict]:
    """Focused higher-angle sweep motivated by the first fixed-box screening pass."""
    cases = []
    height_sets = [
        (0.25, 0.20, 0.25),
        (0.30, 0.20, 0.20),
    ]
    for angles, heights in itertools.product(
        itertools.product(range(62, 66), range(67, 71), range(72, 76)),
        height_sets,
    ):
        label = (
            f"r6_a{angles[0]}_{angles[1]}_{angles[2]}"
            f"_h{int(heights[0] * 100):02d}_{int(heights[1] * 100):02d}_{int(heights[2] * 100):02d}"
        )
        cases.append({"route": "06", "label": label, "angles": angles, "heights": heights})
    return cases


def seed_case() -> dict:
    return {
        "angles": (60, 65, 70),
        "heights": (0.30, 0.20, 0.20),
        "depth_m": DEFAULT_DEPTH_M,
        "battery_top_depth_m": None,
        "battery_half_width_m": 0.075,
        "battery_half_height_m": 0.075,
        "reflectance": 0.85,
        "slope_error_mrad": 2.0,
        "sunshape": "pillbox",
        "yaw_deg": 0.0,
        "pitch_deg": 0.0,
        "dni": DEFAULT_DNI,
    }


def case_scene(case: dict, rays: int, seed: int, default_dni: float) -> dict:
    dni = float(case.get("dni", default_dni))
    if "glass_transmission" in case:
        dni *= float(case["glass_transmission"])
    return fixed_width_scene(
        case["label"],
        tuple(case.get("angles", seed_case()["angles"])),
        tuple(case.get("heights", seed_case()["heights"])),
        rays,
        seed,
        dni,
        depth_m=float(case.get("depth_m", DEFAULT_DEPTH_M)),
        battery_top_depth_m=case.get("battery_top_depth_m"),
        battery_half_width_m=float(case.get("battery_half_width_m", 0.075)),
        battery_half_height_m=float(case.get("battery_half_height_m", 0.075)),
        reflectance=float(case.get("reflectance", 0.85)),
        slope_error_mrad=float(case.get("slope_error_mrad", 2.0)),
        sunshape=str(case.get("sunshape", "pillbox")),
        yaw_deg=float(case.get("yaw_deg", 0.0)),
        pitch_deg=float(case.get("pitch_deg", 0.0)),
    )


def generate_route(route_dir: Path, cases: list[dict], rays: int, seed: int, dni: float) -> list[dict]:
    scene_dir = route_dir / "scenes"
    if scene_dir.exists():
        shutil.rmtree(scene_dir)
    scene_dir.mkdir(parents=True, exist_ok=True)

    manifest_cases = []
    for case in cases:
        root = case_scene(case, rays, seed, dni)
        path = scene_dir / f"{case['label']}.json"
        write_json(path, root)
        manifest_cases.append({
            **json_safe(case),
            "scene": rel(path),
            "box_width_m": BOX_HALF_WIDTH_M * 2.0,
            "interior_finish": INTERIOR_FINISH,
            "rays": rays,
            "rng_seed": seed,
        })
    return manifest_cases


def json_safe(case: dict) -> dict:
    safe = {}
    for key, value in case.items():
        if isinstance(value, tuple):
            safe[key] = list(value)
        else:
            safe[key] = value
    return safe


def read_box_metrics(run_dir: Path, scene: Path, case: dict) -> dict:
    summary = json.loads((run_dir / "summary.json").read_text())
    faces = summary.get("faces", {})
    glass = faces.get("glass_top", {})
    bottom = faces.get("bottom", {})
    battery = faces.get("battery_top")
    wall_power = sum(
        faces.get(name, {}).get("total_power_w", 0.0)
        for name in ["north_wall", "south_wall", "east_wall", "west_wall"]
    )
    battery_power = (
        battery.get("total_power_w", 0.0)
        if battery is not None
        else bottom.get("total_power_w", 0.0)
    )
    center_to_mean = face_center_to_mean(run_dir / "csv" / "flux_glass_top.csv")
    return {
        "label": case["label"],
        "scene": rel(scene),
        "run_dir": rel(run_dir),
        "top_entry_power_w": glass.get("total_power_w", 0.0),
        "absorbed_power_w": summary.get("absorbed_power_w", 0.0),
        "battery_power_w": battery_power,
        "bottom_power_w": bottom.get("total_power_w", 0.0),
        "wall_power_w": wall_power,
        "useful_fraction_pct": (
            battery_power / summary.get("absorbed_power_w", 1.0) * 100.0
            if summary.get("absorbed_power_w", 0.0) > 0.0 else 0.0
        ),
        "center_to_mean_pct": center_to_mean,
        "center_warning": center_to_mean < CENTER_MEAN_WARN_PCT,
        "peak_flux_wm2": glass.get("peak_flux_wm2", 0.0),
        "wall_time_s": summary.get("wall_time_s", 0.0),
    }


def face_center_to_mean(path: Path) -> float:
    with path.open(newline="") as f:
        rows = [[float(value) for value in line.strip().split(",")] for line in f if line.strip()]
    if not rows or not rows[0]:
        return 0.0
    ny = len(rows)
    nx = len(rows[0])
    flat = [value for row in rows for value in row]
    mean_flux = sum(flat) / len(flat)
    if mean_flux <= 0.0:
        return 0.0
    center_cells = [
        rows[ny // 2 - 1][nx // 2 - 1],
        rows[ny // 2 - 1][nx // 2],
        rows[ny // 2][nx // 2 - 1],
        rows[ny // 2][nx // 2],
    ]
    return sum(center_cells) / len(center_cells) / mean_flux * 100.0


def run_route(
    route_dir: Path,
    manifest_cases: list[dict],
    rays: int,
    render_maps: bool = True,
) -> list[dict]:
    records = []
    for case in manifest_cases:
        scene = ROOT / case["scene"]
        run_dir = route_dir / "runs" / case["label"]
        run_headless_flux(scene, rays, run_dir)
        if render_maps and PLOT_BOX.exists():
            subprocess.run(
                [sys.executable, str(PLOT_BOX), str(run_dir)],
                check=True,
                cwd=ROOT,
            )
            image_dir = run_dir / "images"
            if image_dir.exists():
                map_dir = route_dir / "maps" / case["label"]
                if map_dir.exists():
                    shutil.rmtree(map_dir)
                shutil.copytree(image_dir, map_dir)
        records.append(read_box_metrics(run_dir, scene, case))
    return records


def write_manifest(route_dir: Path, title: str, cases: list[dict], args: argparse.Namespace) -> None:
    manifest = {
        "title": title,
        "generated_at_utc": now_iso(),
        "comparison_policy": "Analytic/plane contenders only; CAD/STL 606570 and 6065 excluded.",
        "box_width_m": 0.30,
        "interior_finish": INTERIOR_FINISH,
        "screen_rays": args.screen_rays,
        "finalist_rays": args.finalist_rays,
        "rng_seed": args.seed,
        "dni_wm2": args.dni,
        "cases": cases,
    }
    (route_dir / "manifest.json").write_text(json.dumps(manifest, indent=4) + "\n")


def write_table(route_dir: Path, records: list[dict]) -> None:
    if not records:
        return
    path = route_dir / "tables" / "screen_results.csv"
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(records[0].keys()))
        writer.writeheader()
        writer.writerows(records)


def write_report(route_dir: Path, title: str, cases: list[dict], records: list[dict]) -> None:
    lines = [
        f"# {title}",
        "",
        f"Box width: 0.30 m fixed.",
        f"Interior wall finish: {INTERIOR_FINISH}.",
        "Comparison policy: analytic/plane contenders only; CAD/STL 606570 and 6065 are not ranked here.",
        "",
        f"Generated scenes: {len(cases)}",
    ]
    if not records:
        lines.extend([
            "Run status: not run in this invocation.",
            "",
            "Use `python scripts/fixed_box_analytic_optimization.py --run-screen` to execute screening runs.",
        ])
    else:
        ranked = sorted(records, key=lambda r: r["battery_power_w"], reverse=True)
        lines.extend([
            f"Run status: completed {len(records)} screening runs.",
            "",
            "| Rank | Candidate | Battery W | Absorbed W | Top Entry W | Useful % | C/M % | Status |",
            "|---:|---|---:|---:|---:|---:|---:|---|",
        ])
        for idx, row in enumerate(ranked[:20], start=1):
            status = "WARN" if row["center_warning"] else "OK"
            lines.append(
                f"| {idx} | `{row['label']}` | {row['battery_power_w']:.3f} | "
                f"{row['absorbed_power_w']:.3f} | {row['top_entry_power_w']:.3f} | "
                f"{row['useful_fraction_pct']:.2f} | {row['center_to_mean_pct']:.2f} | {status} |"
            )
    (route_dir / "report.md").write_text("\n".join(lines) + "\n")


def write_top_level(out_root: Path, all_records: list[dict]) -> None:
    readme = [
        "# Fixed-Box Analytic Optimization",
        "",
        "This study keeps the box width fixed at 0.30 m and uses matt black absorbing box walls.",
        "",
        "The CAD/STL `606570` and `6065` scenes include triangular side panels and are excluded from contender rankings. They may be mentioned only as non-comparable context.",
    ]
    (out_root / "README.md").write_text("\n".join(readme) + "\n")

    summary = [
        "# Fixed-Box Analytic Optimization Summary",
        "",
        f"Generated at UTC: {now_iso()}",
        "",
        "Comparison policy: analytic/plane fixed-width contenders only.",
    ]
    if all_records:
        ranked = sorted(all_records, key=lambda r: r["battery_power_w"], reverse=True)
        summary.extend([
            "",
            "| Rank | Route | Candidate | Battery W | Absorbed W | Top Entry W |",
            "|---:|---|---|---:|---:|---:|",
        ])
        for idx, row in enumerate(ranked[:25], start=1):
            summary.append(
                f"| {idx} | {row.get('route', '')} | `{row['label']}` | "
                f"{row['battery_power_w']:.3f} | {row['absorbed_power_w']:.3f} | "
                f"{row['top_entry_power_w']:.3f} |"
            )
    else:
        summary.extend([
            "",
            "No screening runs were executed in this invocation; route reports contain generated scene counts and run commands.",
        ])
    (out_root / "summary_report.md").write_text("\n".join(summary) + "\n")

    csv_path = out_root / "summary_rankings.csv"
    fields = ["route", "label", "battery_power_w", "absorbed_power_w", "top_entry_power_w", "scene", "run_dir"]
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in sorted(all_records, key=lambda r: r["battery_power_w"], reverse=True):
            writer.writerow({field: row.get(field, "") for field in fields})


def write_follow_up_report(route_dir: Path, screen_records: list[dict], finalist_records: list[dict]) -> None:
    lines = [
        "# Focused Higher-Angle Follow-Up",
        "",
        "Box width: 0.30 m fixed.",
        f"Interior wall finish: {INTERIOR_FINISH}.",
        "Comparison policy: analytic/plane contenders only; CAD/STL 606570 and 6065 are not ranked here.",
        "",
        "Screening angle range: 62-65 deg / 67-70 deg / 72-75 deg.",
        "Screening height patterns: 0.25/0.20/0.25 m and 0.30/0.20/0.20 m.",
        f"Screening runs: {len(screen_records)} at 1M rays each.",
        f"Finalist runs: {len(finalist_records)} at 10M rays each.",
        "",
        "## 1M-Ray Screen",
        "",
        "| Rank | Candidate | Battery W | Absorbed W | Top Entry W | Useful % | C/M % | Status |",
        "|---:|---|---:|---:|---:|---:|---:|---|",
    ]
    ranked = sorted(screen_records, key=lambda r: r["battery_power_w"], reverse=True)
    for idx, row in enumerate(ranked[:20], start=1):
        status = "WARN" if row["center_warning"] else "OK"
        lines.append(
            f"| {idx} | `{row['label']}` | {row['battery_power_w']:.3f} | "
            f"{row['absorbed_power_w']:.3f} | {row['top_entry_power_w']:.3f} | "
            f"{row['useful_fraction_pct']:.2f} | {row['center_to_mean_pct']:.2f} | {status} |"
        )

    lines.extend([
        "",
        "## 10M-Ray Finalist Confirmation",
        "",
        "| Rank | Candidate | Battery W | Absorbed W | Top Entry W | Useful % | C/M % | Status |",
        "|---:|---|---:|---:|---:|---:|---:|---|",
    ])
    finalist_ranked = sorted(finalist_records, key=lambda r: r["battery_power_w"], reverse=True)
    for idx, row in enumerate(finalist_ranked, start=1):
        status = "WARN" if row["center_warning"] else "OK"
        lines.append(
            f"| {idx} | `{row['label']}` | {row['battery_power_w']:.3f} | "
            f"{row['absorbed_power_w']:.3f} | {row['top_entry_power_w']:.3f} | "
            f"{row['useful_fraction_pct']:.2f} | {row['center_to_mean_pct']:.2f} | {status} |"
        )
    (route_dir / "report.md").write_text("\n".join(lines) + "\n")


def run_follow_up(args: argparse.Namespace) -> int:
    out_root = args.out_root.resolve()
    route_dir = out_root / "06_focused_higher_angle_followup"
    for child in ["scenes", "runs", "maps", "tables", "finalist_scenes", "finalist_runs"]:
        path = route_dir / child
        if path.exists():
            shutil.rmtree(path)
        path.mkdir(parents=True, exist_ok=True)

    cases = generate_route(route_dir, follow_up_cases(), args.screen_rays, args.seed, args.dni)
    write_manifest(route_dir, "Focused Higher-Angle Follow-Up", cases, args)

    screen_records = run_route(route_dir, cases, args.screen_rays, render_maps=False)
    for row in screen_records:
        row["route"] = "06_focused_higher_angle_followup"
    write_table(route_dir, screen_records)

    finalist_cases = []
    clean = [r for r in screen_records if not r["center_warning"]]
    for row in sorted(clean, key=lambda r: r["battery_power_w"], reverse=True)[:4]:
        case = next(c for c in cases if c["label"] == row["label"])
        finalist_case = dict(case)
        finalist_case["label"] = f"{case['label']}_10m"
        finalist_cases.append(finalist_case)

    finalist_manifest = []
    finalist_scene_dir = route_dir / "finalist_scenes"
    for case in finalist_cases:
        root = case_scene(case, args.finalist_rays, args.seed, args.dni)
        scene_path = finalist_scene_dir / f"{case['label']}.json"
        write_json(scene_path, root)
        finalist_manifest.append({**case, "scene": rel(scene_path)})

    finalist_records = []
    for case in finalist_manifest:
        scene = ROOT / case["scene"]
        run_dir = route_dir / "finalist_runs" / case["label"]
        run_headless_flux(scene, args.finalist_rays, run_dir)
        if PLOT_BOX.exists():
            subprocess.run([sys.executable, str(PLOT_BOX), str(run_dir)], check=True, cwd=ROOT)
        finalist_records.append(read_box_metrics(run_dir, scene, case))

    if screen_records:
        with (route_dir / "tables" / "screen_results.csv").open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(screen_records[0].keys()))
            writer.writeheader()
            writer.writerows(screen_records)
    if finalist_records:
        with (route_dir / "tables" / "finalist_10m_results.csv").open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(finalist_records[0].keys()))
            writer.writeheader()
            writer.writerows(finalist_records)

    write_follow_up_report(route_dir, screen_records, finalist_records)
    print(f"Focused follow-up written to {route_dir}")
    return 0


def main() -> int:
    args = parse_args()
    if args.run_follow_up:
        return run_follow_up(args)

    out_root = args.out_root.resolve()
    if args.clean and out_root.exists():
        shutil.rmtree(out_root)
    route_dirs = ensure_route_dirs(out_root)

    seed = seed_case()
    route_cases: dict[str, list[dict]] = {
        "01_reflector_angle_size_sweep": route1_cases(),
        "02_box_depth_receiver_position_sweep": route2_cases(seed),
        "03_battery_absorber_coupling_sweep": route3_cases(seed),
        "04_greenhouse_optics_sensitivity": route4_cases(seed),
        "05_robustness_sun_error_sweep": route5_cases(seed),
    }
    if args.limit_route1 > 0:
        route_cases["01_reflector_angle_size_sweep"] = route_cases[
            "01_reflector_angle_size_sweep"
        ][: args.limit_route1]

    all_records: list[dict] = []
    for route_slug, title in ROUTES:
        route_dir = route_dirs[route_slug]
        cases = generate_route(route_dir, route_cases[route_slug], args.screen_rays, args.seed, args.dni)
        write_manifest(route_dir, title, cases, args)
        records: list[dict] = []
        if args.run_screen:
            records = run_route(route_dir, cases, args.screen_rays)
            for row in records:
                row["route"] = route_slug
            all_records.extend(records)
            write_table(route_dir, records)
        write_report(route_dir, title, cases, records)

    write_top_level(out_root, all_records)
    print(f"Fixed-box analytic study written to {out_root}")
    if not args.run_screen:
        print("Screening scenes were generated but not run. Add --run-screen to execute them.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
