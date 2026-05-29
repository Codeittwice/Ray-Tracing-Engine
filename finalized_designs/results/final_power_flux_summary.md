# Final Two-Level CAD Power And Flux Summary

DNI: 1000 W/m^2. Ray count: 10,000,000 per PMMA scenario.

| Scenario | Absorbed power (W) | Recorded power (W) | Hits | Wall time (s) |
|---|---:|---:|---:|---:|
| Single PMMA | 202.549 | 468.353 | 16992535 | 70.55 |
| Double PMMA | 179.009 | 416.501 | 56317827 | 213.29 |

## Face Results

| Scenario | Face | Power (W) | Mean flux (W/m^2) | Peak flux (W/m^2) |
|---|---|---:|---:|---:|
| Single PMMA | bottom | 0.000 | 0.0 | 0.0 |
| Single PMMA | battery_top | 48.328 | 2147.9 | 3259.2 |
| Single PMMA | battery_north_wall | 0.117 | 11.1 | 306.6 |
| Single PMMA | battery_south_wall | 0.121 | 11.5 | 327.3 |
| Single PMMA | battery_east_wall | 0.113 | 10.8 | 339.9 |
| Single PMMA | battery_west_wall | 0.122 | 11.6 | 327.4 |
| Double PMMA | bottom | 0.000 | 0.0 | 0.0 |
| Double PMMA | battery_top | 43.902 | 1951.2 | 3144.6 |
| Double PMMA | battery_north_wall | 0.107 | 10.2 | 335.0 |
| Double PMMA | battery_south_wall | 0.112 | 10.6 | 307.8 |
| Double PMMA | battery_east_wall | 0.106 | 10.1 | 250.8 |
| Double PMMA | battery_west_wall | 0.109 | 10.4 | 281.9 |

## Image Outputs

- Single PMMA: `finalized_designs/results/single_pmma_10m/images/*.png`
- Single PMMA shared scale: `finalized_designs/results/single_pmma_10m/images_shared_scale/*.png`
- Double PMMA: `finalized_designs/results/double_pmma_10m/images/*.png`
- Double PMMA shared scale: `finalized_designs/results/double_pmma_10m/images_shared_scale/*.png`
