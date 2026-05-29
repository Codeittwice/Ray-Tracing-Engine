#!/usr/bin/env python3
"""Prepare final two-level CAD meshes and PMMA scene JSON files."""

from __future__ import annotations

import json
import struct
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_MESH_DIR = ROOT / "assets" / "meshes"
FINAL_DIR = ROOT / "finalized_designs"
FINAL_MESH_DIR = FINAL_DIR / "meshes"
FINAL_SCENE_DIR = FINAL_DIR / "scenes"

ENTIRE_STL = SOURCE_MESH_DIR / "two_level_entire_assembly.stl"
REFLECTIVE_STL = SOURCE_MESH_DIR / "two_level_reflective_only.stl"

SCALE_TO_METERS = 0.001
BOX_DEPTH_M = 0.15
BOX_HALF_WIDTH_M = 0.15
BOX_HALF_HEIGHT_M = 0.15
BATTERY_HALF_WIDTH_M = 0.075
BATTERY_HALF_HEIGHT_M = 0.075
BATTERY_HEIGHT_M = 0.07
BATTERY_TOP_DEPTH_M = 0.08
PANE_THICKNESS_M = 0.003
DOUBLE_PANE_GAP_M = 0.010
MESH_ROTATION_EULER_DEG = [90.0, 0.0, 0.0]


Triangle = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]


def read_binary_stl(path: Path) -> list[Triangle]:
    data = path.read_bytes()
    if len(data) < 84:
        raise ValueError(f"{path} is too small to be a binary STL")
    tri_count = struct.unpack_from("<I", data, 80)[0]
    expected = 84 + tri_count * 50
    if expected != len(data):
        raise ValueError(f"{path} is not a supported binary STL")

    triangles: list[Triangle] = []
    offset = 84
    for _ in range(tri_count):
        vals = struct.unpack_from("<12fH", data, offset)
        coords = vals[3:12]
        triangles.append(
            (
                (coords[0], coords[1], coords[2]),
                (coords[3], coords[4], coords[5]),
                (coords[6], coords[7], coords[8]),
            )
        )
        offset += 50
    return triangles


def normal(tri: Triangle) -> tuple[float, float, float]:
    ax, ay, az = tri[0]
    bx, by, bz = tri[1]
    cx, cy, cz = tri[2]
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    length = (nx * nx + ny * ny + nz * nz) ** 0.5
    if length == 0.0:
        return (0.0, 0.0, 0.0)
    return (nx / length, ny / length, nz / length)


def write_binary_stl(path: Path, triangles: list[Triangle], name: str) -> None:
    header = name.encode("ascii", errors="ignore")[:80].ljust(80, b" ")
    with path.open("wb") as file:
        file.write(header)
        file.write(struct.pack("<I", len(triangles)))
        for tri in triangles:
            file.write(struct.pack("<3f", *normal(tri)))
            for point in tri:
                file.write(struct.pack("<3f", *point))
            file.write(struct.pack("<H", 0))


def canonical_triangle(tri: Triangle, quantum: float = 1.0e-5) -> tuple[tuple[int, int, int], ...]:
    return tuple(sorted(tuple(round(coord / quantum) for coord in point) for point in tri))


def centroid_z(tri: Triangle) -> float:
    return sum(point[2] for point in tri) / 3.0


def bounds(triangles: list[Triangle]) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    points = [point for tri in triangles for point in tri]
    return (
        (min(point[0] for point in points), min(point[1] for point in points), min(point[2] for point in points)),
        (max(point[0] for point in points), max(point[1] for point in points), max(point[2] for point in points)),
    )


def rotate_x_pos_90_m(point: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = (coord * SCALE_TO_METERS for coord in point)
    return (x, -z, y)


def transformed_bounds_after_mesh_rotation(
    triangles: list[Triangle],
) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    points = [rotate_x_pos_90_m(point) for tri in triangles for point in tri]
    return (
        (min(point[0] for point in points), min(point[1] for point in points), min(point[2] for point in points)),
        (max(point[0] for point in points), max(point[1] for point in points), max(point[2] for point in points)),
    )


def subtract_reflective(entire: list[Triangle], reflective: list[Triangle]) -> list[Triangle]:
    remaining_reflective = Counter(canonical_triangle(tri) for tri in reflective)
    non_reflective: list[Triangle] = []
    for tri in entire:
        key = canonical_triangle(tri)
        if remaining_reflective[key] > 0:
            remaining_reflective[key] -= 1
        else:
            non_reflective.append(tri)
    return non_reflective


def material(id_: str, type_: str, **kwargs: float | str) -> dict[str, float | str]:
    out: dict[str, float | str] = {"id": id_, "type": type_}
    out.update(kwargs)
    return out


def mesh_element(name: str, mesh_name: str, material_id: str, translation: list[float]) -> dict:
    return {
        "name": name,
        "surface": {
            "type": "mesh",
            "path": f"../meshes/{mesh_name}",
            "scale_to_meters": SCALE_TO_METERS,
        },
        "transform": {
            "translation": translation,
            "rotation_euler_deg": MESH_ROTATION_EULER_DEG,
        },
        "material": material_id,
    }


def pane_interface(name: str, z_m: float, material_id: str, exit_face: bool) -> dict:
    transform: dict[str, list[float]] = {"translation": [0.0, 0.0, z_m]}
    if exit_face:
        transform["rotation_euler_deg"] = [180.0, 0.0, 0.0]
    return {
        "name": name,
        "surface": {
            "type": "plane",
            "half_width": BOX_HALF_WIDTH_M,
            "half_height": BOX_HALF_HEIGHT_M,
        },
        "transform": transform,
        "material": material_id,
    }


def pane_slab(name: str, z_m: float, material_id: str) -> dict:
    return {
        "name": name,
        "surface": {
            "type": "plane",
            "half_width": BOX_HALF_WIDTH_M,
            "half_height": BOX_HALF_HEIGHT_M,
        },
        "transform": {"translation": [0.0, 0.0, z_m]},
        "material": material_id,
    }


def scene(pane_count: int, translation: list[float]) -> dict:
    elements = [
        mesh_element(
            "outer_emergency_blanket_reflectors",
            "two_level_outer_emergency_blanket_reflectors.stl",
            "emergency_blanket",
            translation,
        ),
        mesh_element(
            "inner_aluminum_reflectors",
            "two_level_inner_aluminum_reflectors.stl",
            "aluminum_sheet",
            translation,
        ),
        mesh_element(
            "non_reflective_assembly_occluders",
            "two_level_non_reflective_occluders.stl",
            "non_reflective_absorber",
            translation,
        ),
    ]

    pane_top_z = 0.004
    for pane in range(pane_count):
        base_z = pane_top_z + pane * (PANE_THICKNESS_M + DOUBLE_PANE_GAP_M)
        elements.append(pane_slab(f"pmma_pane_{pane + 1}", base_z, "pmma_pane"))

    return {
        "scene": {
            "name": f"Final two-level CAD design with {pane_count} PMMA pane"
            f"{'' if pane_count == 1 else 's'}",
            "sun": {
                "direction": [0.0, 0.0, -1.0],
                "dni_wm2": 1000.0,
                "sunshape": {"type": "buie", "chi": 0.05},
            },
            "aperture": {
                "type": "disk",
                "center": [0.0, 0.0, 1.0],
                "normal": [0.0, 0.0, 1.0],
                "radius": 0.55,
            },
            "materials": [
                material("aluminum_sheet", "real_mirror", reflectance=0.86, slope_error_mrad=2.0),
                material("emergency_blanket", "real_mirror", reflectance=0.80, slope_error_mrad=5.0),
                material("non_reflective_absorber", "absorber"),
                material(
                    "pmma_pane",
                    "thin_dielectric_pane",
                    n=1.49,
                    thickness_m=PANE_THICKNESS_M,
                    absorption_per_m=1.0,
                ),
            ],
            "elements": elements,
            "receiver": {
                "type": "box",
                "surface": {
                    "type": "plane",
                    "half_width": BOX_HALF_WIDTH_M,
                    "half_height": BOX_HALF_HEIGHT_M,
                },
                "depth": BOX_DEPTH_M,
                "top_mode": "record_pass",
                "grid": {"nx": 64, "ny": 64},
                "transform": {"translation": [0.0, 0.0, 0.0]},
                "battery": {
                    "enabled": True,
                    "half_width": BATTERY_HALF_WIDTH_M,
                    "half_height": BATTERY_HALF_HEIGHT_M,
                    "height_m": BATTERY_HEIGHT_M,
                    "top_depth_m": BATTERY_TOP_DEPTH_M,
                    "nx": 64,
                    "ny": 64,
                },
            },
        },
        "trace": {
            "n_primary_rays": 10000000,
            "max_bounces": 16,
            "power_cutoff_w": 1.0e-9,
            "record_paths": False,
            "max_paths_to_record": 500,
            "rng_seed": 42,
        },
    }


def main() -> int:
    FINAL_MESH_DIR.mkdir(parents=True, exist_ok=True)
    FINAL_SCENE_DIR.mkdir(parents=True, exist_ok=True)
    (FINAL_DIR / "results").mkdir(parents=True, exist_ok=True)

    entire = read_binary_stl(ENTIRE_STL)
    reflective = read_binary_stl(REFLECTIVE_STL)
    ref_min, ref_max = bounds(reflective)
    entire_rot_min, entire_rot_max = transformed_bounds_after_mesh_rotation(entire)
    ref_rot_min, ref_rot_max = transformed_bounds_after_mesh_rotation(reflective)
    rim_z_raw = ref_min[2] + BOX_DEPTH_M / SCALE_TO_METERS

    inner = [tri for tri in reflective if centroid_z(tri) <= rim_z_raw]
    outer = [tri for tri in reflective if centroid_z(tri) > rim_z_raw]
    non_reflective = subtract_reflective(entire, reflective)

    write_binary_stl(FINAL_MESH_DIR / "two_level_inner_aluminum_reflectors.stl", inner,
                     "two_level_inner_aluminum_reflectors")
    write_binary_stl(FINAL_MESH_DIR / "two_level_outer_emergency_blanket_reflectors.stl", outer,
                     "two_level_outer_emergency_blanket_reflectors")
    write_binary_stl(FINAL_MESH_DIR / "two_level_non_reflective_occluders.stl", non_reflective,
                     "two_level_non_reflective_occluders")

    center_x = 0.5 * (ref_rot_min[0] + ref_rot_max[0])
    center_y = 0.5 * (ref_rot_min[1] + ref_rot_max[1])
    translation = [
        -center_x,
        -center_y,
        -BOX_DEPTH_M - entire_rot_min[2],
    ]

    single = scene(1, translation)
    double = scene(2, translation)
    (FINAL_SCENE_DIR / "two_level_final_single_pmma.json").write_text(
        json.dumps(single, indent=4) + "\n",
        encoding="utf-8",
    )
    (FINAL_SCENE_DIR / "two_level_final_double_pmma.json").write_text(
        json.dumps(double, indent=4) + "\n",
        encoding="utf-8",
    )

    manifest = {
        "source_meshes": {
            "entire_assembly": str(ENTIRE_STL.relative_to(ROOT)),
            "reflective_only": str(REFLECTIVE_STL.relative_to(ROOT)),
        },
        "triangle_counts": {
            "entire_assembly": len(entire),
            "reflective_only": len(reflective),
            "inner_aluminum": len(inner),
            "outer_emergency_blanket": len(outer),
            "non_reflective": len(non_reflective),
        },
        "raw_reflective_bounds": {"min": ref_min, "max": ref_max},
        "rotated_entire_bounds_m_before_translation": {
            "min": entire_rot_min,
            "max": entire_rot_max,
        },
        "rotated_reflective_bounds_m_before_translation": {
            "min": ref_rot_min,
            "max": ref_rot_max,
        },
        "mesh_rotation_euler_deg": MESH_ROTATION_EULER_DEG,
        "rim_z_raw": rim_z_raw,
        "scene_mesh_translation_m": translation,
    }
    (FINAL_DIR / "mesh_split_manifest.json").write_text(
        json.dumps(manifest, indent=4) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=4))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
