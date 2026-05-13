#!/usr/bin/env python3
"""Sweep rectangular STL reflector angle scenes and fit simple trends."""

import argparse
import csv
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_ANGLES = [45, 50, 55, 60, 65, 70]
DESIGN_PRIMARY_ANGLES = [55, 60, 65]
DESIGN_FLAP_ANGLE_OFFSETS = [5, 10, 15]
EXTRA_PANEL_WIDTH_M = 0.30
EXTRA_PANEL_HEIGHT_M = 0.20
SCRT_COMPARE = Path("build/debug/scrt_compare.exe")
SCRT_APP = Path("build/debug/scrt_app.exe")
OUT_DIR = Path("results/stl_rect_angle_sweep")
DESIGN_OUT_DIR = Path("results/stl_rect_design_sweep")
DESIGN_3_PANEL_OUT_DIR = Path("results/stl_rect_design_3_panels_sweep")
ANALYTIC_SIZE_OUT_DIR = Path("results/stl_rect_60_65_70_size_sweep")
OUT_CSV = OUT_DIR / "results.csv"
OUT_TXT = OUT_DIR / "summary.txt"
FLUX_CSV = OUT_DIR / "flux_comparison.csv"
FLUX_TXT = OUT_DIR / "flux_comparison.txt"
TEMP_CSV = OUT_DIR / "compare_temp.csv"
APERTURE_RADIUS_M = 0.5
CENTER_MEAN_WARN_PCT = 75.0
BASELINE_PANEL_CENTER_RADIUS_M = 0.225
BASELINE_PANEL_HALF_HEIGHT_M = 0.15


def scene_path(angle):
    return Path(f"examples/stl_rectangular_{angle}deg.json")


def mesh_path(angle):
    return Path(f"assets/meshes/panels_only_rectangular_{angle}deg.stl")


def elongated_mesh_path(angle):
    return Path(f"assets/meshes/panels_only_rectangular_500mm_{angle}deg.stl")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Compare rectangular STL reflector angle scenes."
    )
    parser.add_argument("--rays", type=int, default=100000)
    parser.add_argument("--flux-rays", type=int, default=10_000_000)
    parser.add_argument("--design-screen-rays", type=int, default=1_000_000)
    parser.add_argument("--design-finalist-rays", type=int, default=10_000_000)
    parser.add_argument("--dni", type=float, default=1000.0)
    parser.add_argument("--angles", type=int, nargs="+", default=DEFAULT_ANGLES)
    parser.add_argument("--skip-flux", action="store_true")
    parser.add_argument(
        "--design-sweep",
        action="store_true",
        help="Generate and evaluate baseline STL scenes with 0.3 m x 0.2 m edge extensions.",
    )
    parser.add_argument(
        "--design-3-panel-sweep",
        action="store_true",
        help="Generate and evaluate baseline STL scenes with two chained edge-extension panels.",
    )
    parser.add_argument(
        "--analytic-size-sweep",
        action="store_true",
        help="Generate and evaluate analytic 60/65/70 three-panel size and order candidates.",
    )
    parser.add_argument(
        "--top-offset-z",
        type=float,
        default=0.0,
        help="Reserved for future hinged-panel offsets; current extra panels attach to the baseline outer edge.",
    )
    parser.add_argument(
        "--extra-panel-radius",
        type=float,
        default=0.0,
        help="Reserved for future free-standing auxiliary panels; current extra panels attach to the baseline outer edge.",
    )
    parser.add_argument(
        "--screen-only",
        action="store_true",
        help="Skip the 10M-ray finalist rerun in --design-sweep mode.",
    )
    return parser.parse_args()


def require_inputs(angles):
    missing = []
    if not SCRT_COMPARE.exists():
        missing.append(str(SCRT_COMPARE))
    if not SCRT_APP.exists():
        missing.append(str(SCRT_APP))
    for angle in angles:
        if not mesh_path(angle).exists():
            missing.append(str(mesh_path(angle)))
        if not scene_path(angle).exists():
            missing.append(str(scene_path(angle)))
    if missing:
        print("Missing required files:", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return False
    return True


def run_compare(angles, rays, dni):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    scenes = [str(scene_path(angle)) for angle in angles]
    cmd = [str(SCRT_COMPARE), *scenes, "--rays", str(rays), "--dni", str(dni), "--out", str(TEMP_CSV)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError("scrt_compare failed")

    by_scene = {}
    with open(TEMP_CSV, newline="") as f:
        for row in csv.DictReader(f):
            by_scene[Path(row["scene"]).stem] = row
    TEMP_CSV.unlink(missing_ok=True)
    return by_scene


def run_headless_flux(scene, rays, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(SCRT_APP),
        str(scene),
        "--headless",
        "--rays",
        str(rays),
        "--out",
        str(out_dir),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"scrt_app headless failed for {scene}")


def read_flux_map(path):
    rows = []
    with open(path, newline="") as f:
        for row in csv.reader(f):
            rows.append([float(v) for v in row])
    if not rows or not rows[0]:
        raise RuntimeError(f"empty flux map: {path}")
    return rows


def scene_receiver_size(path):
    with open(path) as f:
        root = json.load(f)
    surf = root["scene"]["receiver"]["surface"]
    return float(surf["half_width"]), float(surf["half_height"])


def analyze_flux_file(label, scene, out_dir):
    flux_path = out_dir / "flux.csv"
    summary_path = out_dir / "summary.json"
    data = read_flux_map(flux_path)
    ny = len(data)
    nx = len(data[0])
    flat = [v for row in data for v in row]
    total_bins = len(flat)
    mean_flux = sum(flat) / total_bins
    peak_flux = max(flat)
    min_flux = min(flat)
    variance = sum((v - mean_flux) ** 2 for v in flat) / total_bins
    std_flux = math.sqrt(variance)

    center_cells = [
        data[ny // 2 - 1][nx // 2 - 1],
        data[ny // 2 - 1][nx // 2],
        data[ny // 2][nx // 2 - 1],
        data[ny // 2][nx // 2],
    ]
    center_flux = sum(center_cells) / len(center_cells)

    peak_j = 0
    peak_i = 0
    for j, row in enumerate(data):
        for i, value in enumerate(row):
            if value == peak_flux:
                peak_i = i
                peak_j = j
                break
        else:
            continue
        break

    half_width, half_height = scene_receiver_size(scene)
    bin_w = 2.0 * half_width / nx
    bin_h = 2.0 * half_height / ny
    peak_x = -half_width + (peak_i + 0.5) * bin_w
    peak_y = -half_height + (peak_j + 0.5) * bin_h
    center_percentile = sum(1 for v in flat if v <= center_flux) / total_bins * 100.0

    total_power = 0.0
    if summary_path.exists():
        with open(summary_path) as f:
            total_power = float(json.load(f)["total_power_w"])

    return {
        "label": label,
        "total_power_w": total_power,
        "center_flux_wm2": center_flux,
        "peak_flux_wm2": peak_flux,
        "mean_flux_wm2": mean_flux,
        "min_flux_wm2": min_flux,
        "std_flux_wm2": std_flux,
        "center_to_peak_pct": center_flux / peak_flux * 100.0 if peak_flux > 0.0 else 0.0,
        "center_to_mean_pct": center_flux / mean_flux * 100.0 if mean_flux > 0.0 else 0.0,
        "center_percentile_pct": center_percentile,
        "peak_x_m": peak_x,
        "peak_y_m": peak_y,
        "flux_csv": str(flux_path),
    }


def analyze_flux(angle):
    return {
        "angle_deg": angle,
        **{
            k: v
            for k, v in analyze_flux_file(
                f"{angle} deg",
                scene_path(angle),
                OUT_DIR / f"{angle}deg",
            ).items()
            if k != "label"
        },
    }


def load_scene_json(path):
    with open(path) as f:
        return json.load(f)


def write_json(path, root):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as f:
        json.dump(root, f, indent=4)
        f.write("\n")


def relative_mesh_path(scene_file, mesh_file):
    rel = Path("../../../") / mesh_file
    return rel.as_posix()


def base_scene_for_design(name, primary_angle):
    root = load_scene_json(scene_path(primary_angle))
    root["scene"]["name"] = name
    root["trace"]["n_primary_rays"] = 1_000_000
    return root


def mesh_element(name, mesh_file, translation):
    return {
        "material": "aluminum",
        "name": name,
        "surface": {
            "path": relative_mesh_path(None, mesh_file),
            "scale_to_meters": 0.001,
            "type": "mesh",
        },
        "transform": {
            "translation": [round(float(v), 7) for v in translation],
        },
    }


def element_translation_for_angle(angle):
    root = load_scene_json(scene_path(angle))
    return root["scene"]["elements"][0]["transform"]["translation"]


def radial_panel_rotation(azimuth_deg, angle_deg):
    if azimuth_deg == 0:
        return [angle_deg, 0.0, 0.0]
    if azimuth_deg == 90:
        return [0.0, -angle_deg, -90.0]
    if azimuth_deg == 180:
        return [-angle_deg, 0.0, 180.0]
    if azimuth_deg == 270:
        return [0.0, angle_deg, 90.0]
    raise ValueError(f"unsupported auxiliary panel azimuth: {azimuth_deg}")


def radial_panel_translation(azimuth_deg, center_radius, center_z):
    theta = math.radians(azimuth_deg)
    x = center_radius * math.sin(theta)
    y = center_radius * math.cos(theta)
    return [round(x, 7), round(y, 7), round(center_z, 7)]


def radial_plane_element(name, azimuth_deg, angle_deg, center_radius, center_z, width_m, height_m):
    return {
        "material": "aluminum",
        "name": name,
        "surface": {
            "half_width": round(width_m / 2.0, 7),
            "half_height": round(height_m / 2.0, 7),
            "type": "plane",
        },
        "transform": {
            "rotation_euler_deg": radial_panel_rotation(azimuth_deg, angle_deg),
            "translation": radial_panel_translation(azimuth_deg, center_radius, center_z),
        },
    }


def baseline_outer_hinge(primary_angle):
    hinge_radius = (
        BASELINE_PANEL_CENTER_RADIUS_M
        + BASELINE_PANEL_HALF_HEIGHT_M * math.cos(math.radians(primary_angle))
    )
    hinge_z = BASELINE_PANEL_HALF_HEIGHT_M * math.sin(math.radians(primary_angle))
    return hinge_radius, hinge_z


def advance_hinge(hinge_radius, hinge_z, panel_angle):
    return (
        hinge_radius + EXTRA_PANEL_HEIGHT_M * math.cos(math.radians(panel_angle)),
        hinge_z + EXTRA_PANEL_HEIGHT_M * math.sin(math.radians(panel_angle)),
    )


def panel_center_from_hinge(hinge_radius, hinge_z, panel_angle):
    return (
        hinge_radius + 0.5 * EXTRA_PANEL_HEIGHT_M * math.cos(math.radians(panel_angle)),
        hinge_z + 0.5 * EXTRA_PANEL_HEIGHT_M * math.sin(math.radians(panel_angle)),
    )


def extension_panel_elements(name_prefix, panel_angle, hinge_radius, hinge_z):
    half_width = EXTRA_PANEL_WIDTH_M / 2.0
    half_height = EXTRA_PANEL_HEIGHT_M / 2.0
    center_radius, center_z = panel_center_from_hinge(hinge_radius, hinge_z, panel_angle)
    elements = []
    for azimuth in [0, 90, 180, 270]:
        elements.append(
            {
                "material": "aluminum",
                "name": f"{name_prefix}_{azimuth}deg_{panel_angle}deg",
                "surface": {
                    "half_width": half_width,
                    "half_height": half_height,
                    "type": "plane",
                },
                "transform": {
                    "rotation_euler_deg": radial_panel_rotation(azimuth, panel_angle),
                    "translation": radial_panel_translation(azimuth, center_radius, center_z),
                },
            }
        )
    return elements


def make_design_scene(
    label,
    primary_angle,
    top_angle=None,
    final_angle=None,
    top_offset_z=0.0,
    extra_panel_radius=0.0,
):
    root = base_scene_for_design(label, primary_angle)
    primary_translation = element_translation_for_angle(primary_angle)
    elements = [
        mesh_element(
            f"primary_{primary_angle}deg",
            mesh_path(primary_angle),
            primary_translation,
        )
    ]

    if top_angle is not None:
        hinge_radius, hinge_z = baseline_outer_hinge(primary_angle)
        elements.extend(extension_panel_elements("extra_panel", top_angle, hinge_radius, hinge_z))
        if final_angle is not None:
            hinge_radius, hinge_z = advance_hinge(hinge_radius, hinge_z, top_angle)
            elements.extend(extension_panel_elements("final_panel", final_angle, hinge_radius, hinge_z))

    root["scene"]["elements"] = elements
    return root


def design_candidates(top_offset_z, extra_panel_radius):
    candidates = []
    for angle in DESIGN_PRIMARY_ANGLES:
        candidates.append(
            {
                "label": f"baseline_{angle}deg",
                "kind": "baseline",
                "primary_angle": angle,
                "top_angle": "",
                "scene": scene_path(angle),
            }
        )

        for offset in DESIGN_FLAP_ANGLE_OFFSETS:
            top_angle = angle + offset
            candidates.append(
                {
                    "label": f"baseline_{angle}deg_extra_{top_angle}deg",
                    "kind": "edge_extensions",
                    "primary_angle": angle,
                    "top_angle": top_angle,
                    "scene": None,
                    "extra_panel_width_m": EXTRA_PANEL_WIDTH_M,
                    "extra_panel_height_m": EXTRA_PANEL_HEIGHT_M,
                }
            )

    return candidates


def design_3_panel_candidates(top_offset_z, extra_panel_radius):
    candidates = []
    for angle in DESIGN_PRIMARY_ANGLES:
        candidates.append(
            {
                "label": f"baseline_{angle}deg",
                "kind": "baseline",
                "primary_angle": angle,
                "top_angle": "",
                "final_angle": "",
                "scene": scene_path(angle),
            }
        )

        for first_offset in DESIGN_FLAP_ANGLE_OFFSETS:
            top_angle = angle + first_offset
            for second_offset in DESIGN_FLAP_ANGLE_OFFSETS:
                final_angle = top_angle + second_offset
                candidates.append(
                    {
                        "label": f"baseline_{angle}deg_extra_{top_angle}deg_final_{final_angle}deg",
                        "kind": "edge_extensions_3_panels",
                        "primary_angle": angle,
                        "top_angle": top_angle,
                        "final_angle": final_angle,
                        "scene": None,
                        "extra_panel_width_m": EXTRA_PANEL_WIDTH_M,
                        "extra_panel_height_m": EXTRA_PANEL_HEIGHT_M,
                    }
                )

    return candidates


def analytic_size_patterns():
    return [
        (0.30, 0.20, 0.20),
        (0.30, 0.25, 0.20),
        (0.25, 0.20, 0.20),
        (0.25, 0.25, 0.25),
        (0.35, 0.20, 0.15),
        (0.25, 0.20, 0.25),
        (0.20, 0.25, 0.25),
    ]


def angle_orders_60_65_70():
    return [
        (60, 65, 70),
        (60, 70, 65),
        (65, 60, 70),
        (65, 70, 60),
        (70, 60, 65),
        (70, 65, 60),
    ]


def analytic_candidate_label(angles, width_m, heights):
    angle_part = "_".join(str(a) for a in angles)
    height_part = "_".join(f"h{int(round(h * 100)):02d}" for h in heights)
    return f"analytic_a{angle_part}_w{int(round(width_m * 100)):02d}_{height_part}"


def make_analytic_size_scene(label, angles, width_m, heights):
    root = base_scene_for_design(label, 60)
    elements = []
    base_angle = angles[0]
    base_height = heights[0]
    hinge_radius = BASELINE_PANEL_CENTER_RADIUS_M - 0.5 * base_height * math.cos(math.radians(base_angle))
    hinge_z = -0.5 * base_height * math.sin(math.radians(base_angle))

    for panel_index, (angle, height) in enumerate(zip(angles, heights)):
        center_radius = hinge_radius + 0.5 * height * math.cos(math.radians(angle))
        center_z = hinge_z + 0.5 * height * math.sin(math.radians(angle))
        for azimuth in [0, 90, 180, 270]:
            elements.append(
                radial_plane_element(
                    f"panel_{panel_index}_{azimuth}deg_{angle}deg",
                    azimuth,
                    angle,
                    center_radius,
                    center_z,
                    width_m,
                    height,
                )
            )
        hinge_radius += height * math.cos(math.radians(angle))
        hinge_z += height * math.sin(math.radians(angle))

    root["scene"]["elements"] = elements
    root["scene"]["name"] = label
    return root


def analytic_size_candidates():
    candidates = []
    widths = [0.25, 0.30, 0.35]
    for angles in angle_orders_60_65_70():
        for width_m in widths:
            for heights in analytic_size_patterns():
                label = analytic_candidate_label(angles, width_m, heights)
                candidates.append(
                    {
                        "label": label,
                        "kind": "analytic_size_order",
                        "primary_angle": angles[0],
                        "top_angle": angles[1],
                        "final_angle": angles[2],
                        "angles": angles,
                        "width_m": width_m,
                        "base_height_m": heights[0],
                        "mid_height_m": heights[1],
                        "final_height_m": heights[2],
                        "scene": None,
                    }
                )
    return candidates


def require_design_inputs(candidates):
    missing = []
    if not SCRT_APP.exists():
        missing.append(str(SCRT_APP))
    for c in candidates:
        scene = c.get("scene")
        if scene is not None and not scene.exists():
            missing.append(str(scene))
        top_angle = c.get("top_angle")
        if isinstance(top_angle, int) and not mesh_path(c["primary_angle"]).exists():
            missing.append(str(mesh_path(c["primary_angle"])))
    if missing:
        print("Missing required design-sweep files:", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return False
    return True


def materialize_design_scenes(candidates, top_offset_z, extra_panel_radius, output_dir):
    scene_dir = output_dir / "scenes"
    if scene_dir.exists():
        shutil.rmtree(scene_dir)
    for c in candidates:
        if c.get("scene") is not None:
            continue
        label = c["label"]
        top_angle = c.get("top_angle")
        root = make_design_scene(
            label,
            c["primary_angle"],
            top_angle if isinstance(top_angle, int) else None,
            c.get("final_angle") if isinstance(c.get("final_angle"), int) else None,
            top_offset_z,
            extra_panel_radius,
        )
        path = scene_dir / f"{label}.json"
        write_json(path, root)
        c["scene"] = path


def materialize_analytic_size_scenes(candidates, output_dir):
    scene_dir = output_dir / "scenes"
    if scene_dir.exists():
        shutil.rmtree(scene_dir)
    for c in candidates:
        root = make_analytic_size_scene(
            c["label"],
            c["angles"],
            c["width_m"],
            (c["base_height_m"], c["mid_height_m"], c["final_height_m"]),
        )
        path = scene_dir / f"{c['label']}.json"
        write_json(path, root)
        c["scene"] = path


def candidate_out_dir(label, pass_name, output_dir):
    return output_dir / pass_name / label


def write_design_comparison(records, csv_path, txt_path, rays, pass_name):
    fieldnames = [
        "label",
        "kind",
        "primary_angle",
        "top_angle",
        "final_angle",
        "extra_panel_width_m",
        "extra_panel_height_m",
        "total_power_w",
        "center_flux_wm2",
        "peak_flux_wm2",
        "mean_flux_wm2",
        "min_flux_wm2",
        "std_flux_wm2",
        "center_to_peak_pct",
        "center_to_mean_pct",
        "center_percentile_pct",
        "center_warning",
        "peak_x_m",
        "peak_y_m",
        "flux_csv",
        "scene",
    ]
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)

    baseline = next((r for r in records if r["label"] == "baseline_60deg"), None)
    baseline_power = baseline["total_power_w"] if baseline is not None else 0.0
    ranked = sorted(records, key=lambda r: r["total_power_w"], reverse=True)
    best_power = ranked[0]
    clean = [
        r for r in ranked
        if not r["center_warning"]
        and (baseline is None or r["total_power_w"] >= baseline_power)
    ]

    def delta_w(record):
        if baseline is None:
            return 0.0
        return record["total_power_w"] - baseline_power

    def geometry_label(record):
        if record["top_angle"] == "":
            return f"base {record['primary_angle']}deg"
        if record.get("final_angle", "") != "":
            return (
                f"base {record['primary_angle']}deg + "
                f"flaps {record['top_angle']}/{record['final_angle']}deg"
            )
        return f"base {record['primary_angle']}deg + flap {record['top_angle']}deg"

    candidate_width = max(34, max(len(r["label"]) for r in records))
    geometry_width = max(25, max(len(geometry_label(r)) for r in records))
    header = (
        f"{'Rank':>4}  {'Candidate':<{candidate_width}} {'Geometry':<{geometry_width}} "
        f"{'Power W':>10} {'dW vs 60':>10} {'Mean':>9} {'Peak':>9} "
        f"{'C/M %':>8} {'C/P %':>8} {'Peak xy m':>18} {'Status':<8}"
    )
    rule = "-" * len(header)

    lines = [
        f"Rectangular STL design comparison ({pass_name})",
        "=" * 78,
        f"Rays per candidate       : {rays:,}",
        f"Center warning threshold : center/mean < {CENTER_MEAN_WARN_PCT:.1f}%",
        "Extra flap geometry      : 0.30 m wide x 0.20 m tall, attached edge-to-edge outward",
        "Ranking objective        : total captured power; center-starved candidates are flagged",
        "",
        f"Best raw power           : {best_power['label']} ({best_power['total_power_w']:.2f} W)",
    ]

    if baseline is not None:
        lines.extend(
            [
                f"60 deg baseline          : {baseline['total_power_w']:.2f} W, "
                f"center/mean {baseline['center_to_mean_pct']:.2f}%",
            ]
        )
    if clean:
        best_clean = clean[0]
        lines.append(
            f"Best no-warning candidate: {best_clean['label']} "
            f"({best_clean['total_power_w']:.2f} W, center/mean {best_clean['center_to_mean_pct']:.2f}%)"
        )

    lines.extend(["", "Ranked Candidates", header, rule])
    for rank, r in enumerate(ranked, start=1):
        status = "WARN" if r["center_warning"] else "OK"
        peak_xy = f"({r['peak_x_m']:+.3f},{r['peak_y_m']:+.3f})"
        lines.append(
            f"{rank:>4}  {r['label']:<{candidate_width}} {geometry_label(r):<{geometry_width}} "
            f"{r['total_power_w']:>10.2f} {delta_w(r):>+10.2f} "
            f"{r['mean_flux_wm2']:>9.0f} {r['peak_flux_wm2']:>9.0f} "
            f"{r['center_to_mean_pct']:>8.2f} {r['center_to_peak_pct']:>8.2f} "
            f"{peak_xy:>18} {status:<8}"
        )

    lines.extend(["", "No-Warning Finalist Shortlist", header, rule])
    shortlist = clean[:5]
    if not shortlist:
        lines.append("No candidate met the no-warning threshold and baseline-power filter.")
    for rank, r in enumerate(shortlist, start=1):
        peak_xy = f"({r['peak_x_m']:+.3f},{r['peak_y_m']:+.3f})"
        lines.append(
            f"{rank:>4}  {r['label']:<{candidate_width}} {geometry_label(r):<{geometry_width}} "
            f"{r['total_power_w']:>10.2f} {delta_w(r):>+10.2f} "
            f"{r['mean_flux_wm2']:>9.0f} {r['peak_flux_wm2']:>9.0f} "
            f"{r['center_to_mean_pct']:>8.2f} {r['center_to_peak_pct']:>8.2f} "
            f"{peak_xy:>18} {'OK':<8}"
        )

    lines.extend(
        [
            "",
            "Legend",
            "  dW vs 60 : total-power delta relative to baseline_60deg.",
            "  C/M %    : center flux divided by mean receiver flux.",
            "  C/P %    : center flux divided by peak receiver flux.",
            "  WARN     : center/mean is below the starvation threshold.",
        ]
    )

    with open(txt_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Design comparison -> {csv_path}")
    print(f"Design summary    -> {txt_path}")


def run_shared_scale_maps(input_dir, output_dir):
    script = Path("scripts/plot_flux_maps.py")
    if not script.exists():
        return
    cmd = [
        sys.executable,
        str(script),
        "--input-dir",
        str(input_dir),
        "--output-dir",
        str(output_dir),
        "--shared-scale",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0 and proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")


def run_design_pass(candidates, rays, pass_name, output_dir):
    pass_dir = output_dir / pass_name
    maps_dir = output_dir / f"{pass_name}_maps"
    if pass_dir.exists():
        shutil.rmtree(pass_dir)
    if maps_dir.exists():
        shutil.rmtree(maps_dir)

    records = []
    for c in candidates:
        label = c["label"]
        out_dir = candidate_out_dir(label, pass_name, output_dir)
        run_headless_flux(c["scene"], rays, out_dir)
        metrics = analyze_flux_file(label, c["scene"], out_dir)
        record = {
            **metrics,
            "kind": c["kind"],
            "primary_angle": c["primary_angle"],
            "top_angle": c["top_angle"],
            "final_angle": c.get("final_angle", ""),
            "extra_panel_width_m": c.get("extra_panel_width_m", ""),
            "extra_panel_height_m": c.get("extra_panel_height_m", ""),
            "center_warning": metrics["center_to_mean_pct"] < CENTER_MEAN_WARN_PCT,
            "scene": str(c["scene"]),
        }
        records.append(record)

    write_design_comparison(
        records,
        output_dir / f"{pass_name}_comparison.csv",
        output_dir / f"{pass_name}_comparison.txt",
        rays,
        pass_name,
    )
    run_shared_scale_maps(output_dir / pass_name, output_dir / f"{pass_name}_maps")
    return records


def run_design_sweep(args, output_dir=DESIGN_OUT_DIR, three_panel=False):
    candidates = (
        design_3_panel_candidates(args.top_offset_z, args.extra_panel_radius)
        if three_panel
        else design_candidates(args.top_offset_z, args.extra_panel_radius)
    )
    if not require_design_inputs(candidates):
        return 1
    materialize_design_scenes(candidates, args.top_offset_z, args.extra_panel_radius, output_dir)
    screen_records = run_design_pass(candidates, args.design_screen_rays, "screen", output_dir)

    if args.screen_only:
        return 0

    baseline = next((r for r in screen_records if r["label"] == "baseline_60deg"), None)
    viable = [
        r for r in screen_records
        if not r["center_warning"]
        and (baseline is None or r["total_power_w"] >= baseline["total_power_w"])
    ]
    if not viable:
        viable = [max(screen_records, key=lambda r: r["total_power_w"])]

    finalist_labels = {"baseline_60deg", *(r["label"] for r in viable)}
    finalists = [c for c in candidates if c["label"] in finalist_labels]
    run_design_pass(finalists, args.design_finalist_rays, "finalists", output_dir)
    return 0


def write_analytic_size_summary(records, output_dir, rays):
    csv_path = output_dir / "screen_comparison.csv"
    txt_path = output_dir / "screen_comparison.txt"
    fieldnames = [
        "label",
        "angles",
        "width_m",
        "base_height_m",
        "mid_height_m",
        "final_height_m",
        "total_power_w",
        "center_flux_wm2",
        "peak_flux_wm2",
        "mean_flux_wm2",
        "min_flux_wm2",
        "std_flux_wm2",
        "center_to_mean_pct",
        "center_to_peak_pct",
        "center_percentile_pct",
        "center_warning",
        "peak_x_m",
        "peak_y_m",
        "scene",
        "flux_csv",
    ]
    output_dir.mkdir(parents=True, exist_ok=True)
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)

    baseline = next(
        r for r in records
        if r["angles"] == "60/65/70"
        and abs(r["width_m"] - 0.30) < 1e-9
        and abs(r["base_height_m"] - 0.30) < 1e-9
        and abs(r["mid_height_m"] - 0.20) < 1e-9
        and abs(r["final_height_m"] - 0.20) < 1e-9
    )
    ranked = sorted(records, key=lambda r: r["total_power_w"], reverse=True)
    clean = [r for r in ranked if not r["center_warning"]]

    def delta(record):
        return record["total_power_w"] - baseline["total_power_w"]

    header = (
        f"{'Rank':>4}  {'Angles':<8} {'W':>5} {'Heights b/m/f':<15} "
        f"{'Power W':>10} {'dW':>9} {'Mean':>8} {'Peak':>8} "
        f"{'C/M %':>8} {'C/P %':>8} {'Peak xy m':>18} {'Status':<6}"
    )
    rule = "-" * len(header)
    lines = [
        "Analytic 60/65/70 size and order sweep",
        "=" * 78,
        f"Rays per candidate       : {rays:,}",
        "Geometry                 : four radial stacks of three analytic rectangular panels",
        "Panel order              : all permutations of 60, 65, 70 degrees",
        "Width sweep              : 0.25, 0.30, 0.35 m",
        "Height patterns          : compact set around 0.30 m base and 0.20 m flaps",
        f"Center warning threshold : center/mean < {CENTER_MEAN_WARN_PCT:.1f}%",
        f"Reference design         : 60/65/70, width 0.30 m, heights 0.30/0.20/0.20 m "
        f"({baseline['total_power_w']:.2f} W)",
        "",
        f"Best raw power           : {ranked[0]['label']} ({ranked[0]['total_power_w']:.2f} W)",
    ]
    if clean:
        lines.append(
            f"Best no-warning candidate: {clean[0]['label']} "
            f"({clean[0]['total_power_w']:.2f} W, center/mean {clean[0]['center_to_mean_pct']:.2f}%)"
        )

    lines.extend(["", "Top 25 Ranked Candidates", header, rule])
    for rank, r in enumerate(ranked[:25], start=1):
        status = "WARN" if r["center_warning"] else "OK"
        heights = f"{r['base_height_m']:.2f}/{r['mid_height_m']:.2f}/{r['final_height_m']:.2f}"
        peak_xy = f"({r['peak_x_m']:+.3f},{r['peak_y_m']:+.3f})"
        lines.append(
            f"{rank:>4}  {r['angles']:<8} {r['width_m']:>5.2f} {heights:<15} "
            f"{r['total_power_w']:>10.2f} {delta(r):>+9.2f} "
            f"{r['mean_flux_wm2']:>8.0f} {r['peak_flux_wm2']:>8.0f} "
            f"{r['center_to_mean_pct']:>8.2f} {r['center_to_peak_pct']:>8.2f} "
            f"{peak_xy:>18} {status:<6}"
        )

    lines.extend(["", "Top 10 No-Warning Candidates", header, rule])
    for rank, r in enumerate(clean[:10], start=1):
        heights = f"{r['base_height_m']:.2f}/{r['mid_height_m']:.2f}/{r['final_height_m']:.2f}"
        peak_xy = f"({r['peak_x_m']:+.3f},{r['peak_y_m']:+.3f})"
        lines.append(
            f"{rank:>4}  {r['angles']:<8} {r['width_m']:>5.2f} {heights:<15} "
            f"{r['total_power_w']:>10.2f} {delta(r):>+9.2f} "
            f"{r['mean_flux_wm2']:>8.0f} {r['peak_flux_wm2']:>8.0f} "
            f"{r['center_to_mean_pct']:>8.2f} {r['center_to_peak_pct']:>8.2f} "
            f"{peak_xy:>18} {'OK':<6}"
        )

    lines.extend(
        [
            "",
            "Notes",
            "  This is an analytic sizing proxy for CAD, not an STL import.",
            "  It does not include triangular side filler panels yet; those should be CAD-modeled or imported as meshes.",
        ]
    )
    with open(txt_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Analytic size comparison -> {csv_path}")
    print(f"Analytic size summary    -> {txt_path}")


def run_analytic_size_sweep(args):
    candidates = analytic_size_candidates()
    if not SCRT_APP.exists():
        print(f"Missing required file: {SCRT_APP}", file=sys.stderr)
        return 1
    materialize_analytic_size_scenes(candidates, ANALYTIC_SIZE_OUT_DIR)

    pass_dir = ANALYTIC_SIZE_OUT_DIR / "screen"
    maps_dir = ANALYTIC_SIZE_OUT_DIR / "screen_maps"
    if pass_dir.exists():
        shutil.rmtree(pass_dir)
    if maps_dir.exists():
        shutil.rmtree(maps_dir)

    records = []
    for c in candidates:
        out_dir = candidate_out_dir(c["label"], "screen", ANALYTIC_SIZE_OUT_DIR)
        run_headless_flux(c["scene"], args.design_screen_rays, out_dir)
        metrics = analyze_flux_file(c["label"], c["scene"], out_dir)
        records.append(
            {
                "label": c["label"],
                "angles": "/".join(str(a) for a in c["angles"]),
                "width_m": c["width_m"],
                "base_height_m": c["base_height_m"],
                "mid_height_m": c["mid_height_m"],
                "final_height_m": c["final_height_m"],
                **metrics,
                "center_warning": metrics["center_to_mean_pct"] < CENTER_MEAN_WARN_PCT,
                "scene": str(c["scene"]),
            }
        )

    write_analytic_size_summary(records, ANALYTIC_SIZE_OUT_DIR, args.design_screen_rays)
    run_shared_scale_maps(ANALYTIC_SIZE_OUT_DIR / "screen", maps_dir)
    return 0


def write_flux_comparison(records, flux_rays):
    with open(FLUX_CSV, "w", newline="") as f:
        fieldnames = [
            "angle_deg",
            "total_power_w",
            "center_flux_wm2",
            "peak_flux_wm2",
            "mean_flux_wm2",
            "min_flux_wm2",
            "std_flux_wm2",
            "center_to_peak_pct",
            "center_to_mean_pct",
            "center_percentile_pct",
            "peak_x_m",
            "peak_y_m",
            "flux_csv",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)

    coolest_center = min(records, key=lambda r: r["center_to_mean_pct"])
    hottest_peak = max(records, key=lambda r: r["peak_flux_wm2"])
    best_power = max(records, key=lambda r: r["total_power_w"])

    lines = [
        "Rectangular STL flux-map comparison",
        f"flux rays per scene: {flux_rays}",
        "",
        "Center metrics use the average of the four central receiver bins.",
        "",
        "Per-angle flux-map metrics:",
    ]
    for r in records:
        lines.append(
            f"  {r['angle_deg']:>3} deg  center={r['center_flux_wm2']:.4f} W/m^2  "
            f"peak={r['peak_flux_wm2']:.4f} W/m^2  mean={r['mean_flux_wm2']:.4f} W/m^2  "
            f"center/mean={r['center_to_mean_pct']:.2f}%  "
            f"center/peak={r['center_to_peak_pct']:.2f}%  "
            f"peak_xy=({r['peak_x_m']:.4f}, {r['peak_y_m']:.4f}) m"
        )

    lines.extend(
        [
            "",
            f"Lowest center/mean ratio: {coolest_center['angle_deg']} deg "
            f"({coolest_center['center_to_mean_pct']:.2f}%)",
            f"Highest peak flux: {hottest_peak['angle_deg']} deg "
            f"({hottest_peak['peak_flux_wm2']:.4f} W/m^2)",
            f"Highest total power: {best_power['angle_deg']} deg "
            f"({best_power['total_power_w']:.4f} W)",
        ]
    )

    with open(FLUX_TXT, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Flux comparison -> {FLUX_CSV}")
    print(f"Flux summary    -> {FLUX_TXT}")


def fit_quadratic(xs, ys):
    n = len(xs)
    if n < 3:
        return None

    sx0 = float(n)
    sx1 = sum(xs)
    sx2 = sum(x * x for x in xs)
    sx3 = sum(x * x * x for x in xs)
    sx4 = sum(x * x * x * x for x in xs)
    sy0 = sum(ys)
    sy1 = sum(x * y for x, y in zip(xs, ys))
    sy2 = sum(x * x * y for x, y in zip(xs, ys))

    matrix = [
        [sx4, sx3, sx2, sy2],
        [sx3, sx2, sx1, sy1],
        [sx2, sx1, sx0, sy0],
    ]

    for col in range(3):
        pivot = max(range(col, 3), key=lambda r: abs(matrix[r][col]))
        if abs(matrix[pivot][col]) < 1e-12:
            return None
        matrix[col], matrix[pivot] = matrix[pivot], matrix[col]
        div = matrix[col][col]
        for c in range(col, 4):
            matrix[col][c] /= div
        for r in range(3):
            if r == col:
                continue
            factor = matrix[r][col]
            for c in range(col, 4):
                matrix[r][c] -= factor * matrix[col][c]

    a, b, c = matrix[0][3], matrix[1][3], matrix[2][3]
    if abs(a) < 1e-12:
        optimum = None
    else:
        optimum = -b / (2.0 * a)
        optimum = min(max(optimum, min(xs)), max(xs))
    return a, b, c, optimum


def write_outputs(rows, angles, rays, dni):
    aperture_area = math.pi * APERTURE_RADIUS_M * APERTURE_RADIUS_M
    records = []
    for angle in angles:
        key = scene_path(angle).stem
        row = rows[key]
        power = float(row["total_power_w"])
        peak = float(row["peak_flux_wm2"])
        cr = float(row["concentration_ratio"])
        wall = float(row["wall_time_s"])
        efficiency = power / (dni * aperture_area) * 100.0
        records.append(
            {
                "angle_deg": angle,
                "total_power_w": power,
                "peak_flux_wm2": peak,
                "concentration_ratio": cr,
                "efficiency_pct": efficiency,
                "wall_time_s": wall,
            }
        )

    with open(OUT_CSV, "w", newline="") as f:
        fieldnames = [
            "angle_deg",
            "total_power_w",
            "peak_flux_wm2",
            "concentration_ratio",
            "efficiency_pct",
            "wall_time_s",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)

    best_power = max(records, key=lambda r: r["total_power_w"])
    best_cr = max(records, key=lambda r: r["concentration_ratio"])
    xs = [float(r["angle_deg"]) for r in records]
    power_fit = fit_quadratic(xs, [r["total_power_w"] for r in records])
    cr_fit = fit_quadratic(xs, [r["concentration_ratio"] for r in records])

    lines = [
        "Rectangular STL angle sweep",
        f"rays: {rays}",
        f"dni_wm2: {dni:g}",
        "",
        "Measured results:",
    ]
    for r in records:
        lines.append(
            f"  {r['angle_deg']:>3} deg  power={r['total_power_w']:.4f} W  "
            f"peak={r['peak_flux_wm2']:.4f} W/m^2  "
            f"CR={r['concentration_ratio']:.6f}x  eta={r['efficiency_pct']:.4f}%"
        )

    lines.extend(
        [
            "",
            f"Best measured total power: {best_power['angle_deg']} deg "
            f"({best_power['total_power_w']:.4f} W)",
            f"Best measured peak CR: {best_cr['angle_deg']} deg "
            f"({best_cr['concentration_ratio']:.6f}x)",
        ]
    )

    if power_fit:
        a, b, c, optimum = power_fit
        lines.append(
            f"Quadratic total-power fit: y = {a:.8g} x^2 + {b:.8g} x + {c:.8g}; "
            f"optimum ~= {optimum:.3f} deg"
        )
    if cr_fit:
        a, b, c, optimum = cr_fit
        lines.append(
            f"Quadratic CR fit: y = {a:.8g} x^2 + {b:.8g} x + {c:.8g}; "
            f"optimum ~= {optimum:.3f} deg"
        )

    with open(OUT_TXT, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Results -> {OUT_CSV}")
    print(f"Summary -> {OUT_TXT}")
    print()
    print("\n".join(lines))


def main():
    args = parse_args()
    if args.design_sweep:
        return run_design_sweep(args, DESIGN_OUT_DIR, three_panel=False)
    if args.design_3_panel_sweep:
        return run_design_sweep(args, DESIGN_3_PANEL_OUT_DIR, three_panel=True)
    if args.analytic_size_sweep:
        return run_analytic_size_sweep(args)

    angles = sorted(args.angles)
    if not require_inputs(angles):
        return 1
    rows = run_compare(angles, args.rays, args.dni)
    write_outputs(rows, angles, args.rays, args.dni)

    if not args.skip_flux:
        flux_records = []
        for angle in angles:
            run_headless_flux(
                scene_path(angle),
                args.flux_rays,
                OUT_DIR / f"{angle}deg",
            )
            flux_records.append(analyze_flux(angle))
        write_flux_comparison(flux_records, args.flux_rays)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
