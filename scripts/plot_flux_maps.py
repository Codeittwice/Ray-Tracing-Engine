#!/usr/bin/env python3
"""Render flux.csv files from STL angle sweeps as heatmap PNG images."""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert per-angle flux.csv files into heatmap PNG images."
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("results/stl_rect_angle_sweep"),
        help="Directory containing per-angle subdirectories such as 45deg/flux.csv.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory for PNG images. Defaults to --input-dir.",
    )
    parser.add_argument(
        "--cmap",
        default="inferno",
        help="Matplotlib colormap name.",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=200,
        help="PNG output resolution.",
    )
    parser.add_argument(
        "--shared-scale",
        action="store_true",
        help="Use one color scale across all flux maps.",
    )
    return parser.parse_args()


def find_flux_files(input_dir):
    return sorted(input_dir.glob("*/flux.csv"))


def load_maps(flux_files):
    maps = []
    for flux_csv in flux_files:
        maps.append((flux_csv, np.loadtxt(flux_csv, delimiter=",")))
    return maps


def render_map(flux_csv, data, output_dir, cmap, dpi, vmin=None, vmax=None):
    angle = flux_csv.parent.name
    out = output_dir / f"flux_{angle}.png"

    plt.figure(figsize=(6, 5))
    plt.imshow(data, origin="lower", cmap=cmap, vmin=vmin, vmax=vmax)
    plt.colorbar(label="Flux (W/m^2)")
    plt.title(f"Rectangular STL {angle}")
    plt.xlabel("receiver x bin")
    plt.ylabel("receiver y bin")
    plt.tight_layout()
    plt.savefig(out, dpi=dpi)
    plt.close()
    return out


def main():
    args = parse_args()
    output_dir = args.output_dir or args.input_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    flux_files = find_flux_files(args.input_dir)
    if not flux_files:
        print(f"No flux.csv files found under {args.input_dir}")
        return 1

    maps = load_maps(flux_files)
    vmin = vmax = None
    if args.shared_scale:
        vmin = min(float(np.min(data)) for _, data in maps)
        vmax = max(float(np.max(data)) for _, data in maps)

    for flux_csv, data in maps:
        out = render_map(flux_csv, data, output_dir, args.cmap, args.dpi, vmin, vmax)
        print(out)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
