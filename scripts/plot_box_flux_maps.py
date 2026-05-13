#!/usr/bin/env python3
"""Render multi-face box receiver flux CSVs as separate and unfolded PNG maps."""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


FACES = [
    "glass_top",
    "bottom",
    "north_wall",
    "south_wall",
    "east_wall",
    "west_wall",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render box receiver face CSVs into separate and unfolded flux maps."
    )
    parser.add_argument(
        "run_dir",
        type=Path,
        help="Box receiver run directory containing csv/flux_<face>.csv files.",
    )
    parser.add_argument("--cmap", default="inferno")
    parser.add_argument("--dpi", type=int, default=180)
    parser.add_argument(
        "--vmax",
        type=float,
        default=None,
        help="Fixed color-scale maximum in W/m². Defaults to the run's own maximum.",
    )
    return parser.parse_args()


def load_faces(csv_dir: Path) -> dict[str, np.ndarray]:
    maps: dict[str, np.ndarray] = {}
    for face in FACES:
        path = csv_dir / f"flux_{face}.csv"
        if path.exists():
            maps[face] = np.loadtxt(path, delimiter=",")
    missing = [face for face in FACES if face not in maps]
    if missing:
        raise FileNotFoundError(
            f"missing box receiver CSVs in {csv_dir}: {', '.join(missing)}"
        )
    return maps


def shared_limits(maps: dict[str, np.ndarray], fixed_vmax: float | None) -> tuple[float, float]:
    vmax = fixed_vmax if fixed_vmax is not None else max(float(np.nanmax(data)) for data in maps.values())
    return 0.0, vmax if vmax > 0.0 else 1.0


def save_face_images(
    maps: dict[str, np.ndarray],
    image_dir: Path,
    cmap: str,
    dpi: int,
    vmin: float,
    vmax: float,
) -> None:
    for face, data in maps.items():
        fig, ax = plt.subplots(figsize=(5.0, 4.0), constrained_layout=True)
        im = ax.imshow(data, origin="lower", cmap=cmap, vmin=vmin, vmax=vmax)
        ax.set_title(face)
        ax.set_xlabel("u bin")
        ax.set_ylabel("v bin")
        fig.colorbar(im, ax=ax, label="Flux (W/m²)")
        fig.savefig(image_dir / f"flux_{face}.png", dpi=dpi)
        plt.close(fig)


def paste(canvas: np.ndarray, data: np.ndarray, row: int, col: int) -> None:
    h, w = data.shape
    canvas[row : row + h, col : col + w] = data


def save_unfolded(
    maps: dict[str, np.ndarray],
    image_dir: Path,
    cmap: str,
    dpi: int,
    vmin: float,
    vmax: float,
) -> None:
    bottom = maps["bottom"]
    north = maps["north_wall"]
    south = np.flipud(maps["south_wall"])
    east = np.fliplr(maps["east_wall"])
    west = maps["west_wall"]
    top = maps["glass_top"]

    bottom_h, bottom_w = bottom.shape
    wall_depth = north.shape[0]
    side_depth = west.shape[1]
    gap = max(4, bottom_w // 16)

    cross_h = wall_depth + bottom_h + south.shape[0]
    cross_w = side_depth + bottom_w + east.shape[1]
    canvas_h = max(cross_h, top.shape[0])
    canvas_w = cross_w + gap + top.shape[1]
    canvas = np.full((canvas_h, canvas_w), np.nan)

    origin_r = wall_depth
    origin_c = side_depth
    paste(canvas, north, 0, origin_c)
    paste(canvas, west, origin_r, 0)
    paste(canvas, bottom, origin_r, origin_c)
    paste(canvas, east, origin_r, origin_c + bottom_w)
    paste(canvas, south, origin_r + bottom_h, origin_c)
    paste(canvas, top, origin_r, cross_w + gap)

    fig, ax = plt.subplots(figsize=(9.0, 6.0), constrained_layout=True)
    im = ax.imshow(canvas, origin="upper", cmap=cmap, vmin=vmin, vmax=vmax)
    ax.set_title("Unfolded box receiver flux")
    ax.set_xticks([])
    ax.set_yticks([])

    labels = {
        "north_wall": (origin_c + bottom_w / 2, wall_depth / 2),
        "west_wall": (side_depth / 2, origin_r + bottom_h / 2),
        "bottom": (origin_c + bottom_w / 2, origin_r + bottom_h / 2),
        "east_wall": (origin_c + bottom_w + east.shape[1] / 2, origin_r + bottom_h / 2),
        "south_wall": (origin_c + bottom_w / 2, origin_r + bottom_h + south.shape[0] / 2),
        "glass_top": (cross_w + gap + top.shape[1] / 2, origin_r + top.shape[0] / 2),
    }
    for label, (x, y) in labels.items():
        ax.text(
            x,
            y,
            label,
            ha="center",
            va="center",
            color="white",
            fontsize=8,
            bbox={"facecolor": "black", "alpha": 0.35, "edgecolor": "none", "pad": 2},
        )

    fig.colorbar(im, ax=ax, label="Flux (W/m²)")
    fig.savefig(image_dir / "unfolded_box_flux.png", dpi=dpi)
    plt.close(fig)


def main() -> int:
    args = parse_args()
    csv_dir = args.run_dir / "csv"
    image_dir = args.run_dir / "images"
    image_dir.mkdir(parents=True, exist_ok=True)

    maps = load_faces(csv_dir)
    vmin, vmax = shared_limits(maps, args.vmax)
    save_face_images(maps, image_dir, args.cmap, args.dpi, vmin, vmax)
    save_unfolded(maps, image_dir, args.cmap, args.dpi, vmin, vmax)
    print(f"Wrote box receiver images to {image_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
