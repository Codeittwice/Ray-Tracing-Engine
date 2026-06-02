# Final Two-Level CAD Power And Flux Summary

DNI: 1000 W/m^2. Ray count: 10,000,000 per PMMA scenario.

| Scenario | Absorbed power (W) | Recorded power (W) | Hits | Wall time (s) |
|---|---:|---:|---:|---:|
| Single PMMA | 277.953 | 553.667 | 17322047 | 147.90 |
| Double PMMA | 248.382 | 494.642 | 57580243 | 772.73 |

## Face Results

| Scenario | Face | Power (W) | Mean flux (W/m^2) | Peak flux (W/m^2) |
|---|---|---:|---:|---:|
| Single PMMA | bottom | 62.601 | 695.6 | 1168.7 |
| Single PMMA | battery_top | 52.202 | 2320.1 | 3503.6 |
| Single PMMA | battery_north_wall | 0.124 | 11.8 | 331.8 |
| Single PMMA | battery_south_wall | 0.127 | 12.1 | 341.4 |
| Single PMMA | battery_east_wall | 0.121 | 11.5 | 340.0 |
| Single PMMA | battery_west_wall | 0.130 | 12.3 | 314.8 |
| Double PMMA | bottom | 58.057 | 645.1 | 1077.6 |
| Double PMMA | battery_top | 47.365 | 2105.1 | 3139.1 |
| Double PMMA | battery_north_wall | 0.114 | 10.8 | 349.7 |
| Double PMMA | battery_south_wall | 0.118 | 11.2 | 296.4 |
| Double PMMA | battery_east_wall | 0.114 | 10.9 | 285.0 |
| Double PMMA | battery_west_wall | 0.115 | 10.9 | 259.1 |

## Image Outputs

- Single PMMA: `finalized_designs/results/single_pmma_10m/images/*.png`
- Single PMMA shared scale: `finalized_designs/results/single_pmma_10m/images_shared_scale/*.png`
- Single PMMA per-face scale: `finalized_designs/results/single_pmma_10m/images_per_face_scale/*.png`
- Single PMMA log scale: `finalized_designs/results/single_pmma_10m/images_log_scale/*.png`
- Double PMMA: `finalized_designs/results/double_pmma_10m/images/*.png`
- Double PMMA shared scale: `finalized_designs/results/double_pmma_10m/images_shared_scale/*.png`
- Double PMMA per-face scale: `finalized_designs/results/double_pmma_10m/images_per_face_scale/*.png`
- Double PMMA log scale: `finalized_designs/results/double_pmma_10m/images_log_scale/*.png`
