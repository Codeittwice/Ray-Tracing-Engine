#!/usr/bin/env python3
"""Write a compact Markdown comparison for final two-level PMMA simulations."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULTS_DIR = ROOT / "finalized_designs" / "results"
RUNS = [
    ("single_pmma_10m", "Single PMMA"),
    ("double_pmma_10m", "Double PMMA"),
]
REPORT_FACES = [
    "bottom",
    "battery_top",
    "battery_north_wall",
    "battery_south_wall",
    "battery_east_wall",
    "battery_west_wall",
]


def load_summary(run_dir: Path) -> dict:
    path = run_dir / "summary.json"
    if not path.exists():
        raise FileNotFoundError(path)
    return json.loads(path.read_text(encoding="utf-8"))


def face_row(label: str, face: str, data: dict) -> str:
    faces = data["faces"]
    values = faces.get(face)
    if values is None:
        return f"| {label} | {face} | n/a | n/a | n/a |\n"
    return (
        f"| {label} | {face} | {values['total_power_w']:.3f} | "
        f"{values['mean_flux_wm2']:.1f} | {values['peak_flux_wm2']:.1f} |\n"
    )


def main() -> int:
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    summaries = [(name, label, load_summary(RESULTS_DIR / name)) for name, label in RUNS]

    out = []
    out.append("# Final Two-Level CAD Power And Flux Summary\n\n")
    out.append("DNI: 1000 W/m^2. Ray count: 10,000,000 per PMMA scenario.\n\n")
    out.append("| Scenario | Absorbed power (W) | Recorded power (W) | Hits | Wall time (s) |\n")
    out.append("|---|---:|---:|---:|---:|\n")
    for _name, label, summary in summaries:
        out.append(
            f"| {label} | {summary['absorbed_power_w']:.3f} | "
            f"{summary['recorded_power_w']:.3f} | {summary['total_hits']} | "
            f"{summary['wall_time_s']:.2f} |\n"
        )

    out.append("\n## Face Results\n\n")
    out.append("| Scenario | Face | Power (W) | Mean flux (W/m^2) | Peak flux (W/m^2) |\n")
    out.append("|---|---|---:|---:|---:|\n")
    for _name, label, summary in summaries:
        for face in REPORT_FACES:
            out.append(face_row(label, face, summary))

    out.append("\n## Image Outputs\n\n")
    for name, label, _summary in summaries:
        out.append(f"- {label}: `finalized_designs/results/{name}/images/*.png`\n")
        out.append(f"- {label} shared scale: `finalized_designs/results/{name}/images_shared_scale/*.png`\n")
        out.append(f"- {label} per-face scale: `finalized_designs/results/{name}/images_per_face_scale/*.png`\n")
        out.append(f"- {label} log scale: `finalized_designs/results/{name}/images_log_scale/*.png`\n")

    (RESULTS_DIR / "final_power_flux_summary.md").write_text("".join(out), encoding="utf-8")
    print(f"Wrote {RESULTS_DIR / 'final_power_flux_summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
