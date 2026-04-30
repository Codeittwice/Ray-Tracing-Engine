# Implementation Plan — Post-Phase-7 Gap Closure

Four items approved by the user. Execute in order; commit each separately before starting the next.

---

## Item 1 — CylindricalParaboloid surface  ✅ IN PROGRESS
**Status:** Implementing

### What
New `CylindricalParaboloid` surface (x²=4fz, extruded in y) — models a parabolic trough
concentrator whose focal line is at (0, y, f) for all y.

### Geometry
- Equation: F(x,y,z) = x² − 4fz = 0
- Ray intersection: solve dx²·t² + (2·ox·dx − 4f·dz)·t + (ox² − 4f·oz) = 0
- Aperture clip: |x| ≤ half_width, |y| ≤ half_length, z ≥ 0
- Normal: ∇F = (2x, 0, −4f), normalized; front-face logic same as Paraboloid
- Tessellation: rectangular (x,y) grid, z = x²/(4f) at each vertex

### Files
| Action | File |
|--------|------|
| CREATE | `include/scrt/surfaces/CylindricalParaboloid.hpp` |
| CREATE | `src/surfaces/CylindricalParaboloid.cpp` |
| CREATE | `examples/parabolic_trough.json` |
| MODIFY | `src/io/SceneLoader.cpp` — add dispatch for `"cylindrical_paraboloid"` |
| MODIFY | `tests/test_intersections.cpp` — add T_CP1 and T_CP2 |
| MODIFY | `CMakeLists.txt` — add new .cpp to scrt_core |

### JSON schema
```json
{
  "type": "cylindrical_paraboloid",
  "focal_length_m": 0.3,
  "aperture_half_width_m": 0.25,
  "aperture_half_length_m": 0.5
}
```

### Tests
- **T_CP1**: Ray (0.4, 0.3, 100) dir (0,0,−1) → hit at z=0.08, normal ∝ (0.8, 0, −2.0), y-coordinate of hit = 0.3
- **T_CP2**: PerfectMirror attached; 4 x-offset rays; all reflect through focal line x≈0 at z=f within 1e-9 m

### Acceptance
1. Build clean, no new warnings
2. All ctest pass including T_CP1, T_CP2
3. `scrt_app examples/parabolic_trough.json` runs without crash
4. Commit: `"Item 1: CylindricalParaboloid surface, trough example, T_CP1/T_CP2 tests"`

---

## Item 2 — Wavelength-dependent absorption in Dielectric
**Status:** Pending

### What
Extend `Dielectric` so that `α` (absorption coefficient, m⁻¹) is a piecewise-linear
function of wavelength (nm), enabling realistic greenhouse-effect glazing modelling.

### Key design
- New data member: `std::vector<std::pair<double,double>> alpha_spectrum_` (wavelength_nm, alpha_per_m)
- If spectrum is empty, fall back to existing scalar `absorption_per_m_`
- `alpha_at(double wl_nm)` interpolates linearly; clamps outside range to nearest endpoint
- `Ray` or `Hit` carries current wavelength (if not already); tracer samples it from sun spectrum
- JSON key: `"alpha_spectrum": [[300, 0.0], [700, 0.5], [2500, 120.0]]` (arrays of [nm, 1/m])
- SceneLoader: parse optional `alpha_spectrum` array

### Tests
- T_WA1: alpha_at() interpolation: at midpoint of two nodes returns expected linear value
- T_WA2: Beer-Lambert: slab of thickness 0.01 m, alpha(500nm)=100 m⁻¹ → transmittance ≈ e^(−1) ≈ 0.368

---

## Item 3 — FresnelZoneLens surface
**Status:** Pending

### What
New `FresnelZoneLens` surface: flat disc divided into concentric annular zones,
each refracting independently with its own local prism angle to approximate a flat Fresnel lens.

### Key design
- Parameters: `inner_radius`, `pitch` (zone width), `n_zones`, `n_lens` (refractive index)
- Each zone i spans [inner + i·pitch, inner + (i+1)·pitch]
- Local prism tilt angle per zone chosen to refract paraxial ray toward focal point
- Surface is planar (z=0); hit is computed as ray–plane intersection, then zone selected by radius
- Normal for refraction is the zone's tilted facet normal, not the global plane normal

### Tests
- T_FZL1: On-axis ray through zone i → deflected to focal point within 1e-6 m (paraxial)
- T_FZL2: Energy conservation — no power created across a zone boundary

---

## Item 4 — Multi-scene comparison driver
**Status:** Pending

### What
New CLI binary `scrt_compare` (or `--compare` flag on `scrt_app`) that runs N scene JSONs
sequentially and writes `summary.csv` with columns:
`scene, total_power_w, peak_flux_wm2, concentration_ratio, wall_time_s`

### Key design
- Thin `main` in `app/compare_main.cpp`; calls existing `SceneLoader`, `Tracer`, `ResultsExporter`
- Concentration ratio = peak_flux / DNI
- CSV written with `std::ofstream`, no extra dependencies
- New CMake target `scrt_compare`

### Tests
- T_MC1: Run two trivial scenes (already in examples/); verify CSV has 2 rows with correct scene names
- T_MC2: Verify wall_time_s > 0 for each row

---

## Notes
- All SI units; no `shared_ptr`; one-line Doxygen on every public symbol
- After each item: build + ctest + commit before starting next
