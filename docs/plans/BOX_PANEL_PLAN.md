# Plan: Fix box_cooker + Add panel_cooker

## box_cooker.json — problems fixed
- Was 2-panel V-groove with swapped rotations (reflectors aimed AWAY from pot)
- Now 3-panel Milikias design: back (north), east, west
- Rotations fixed: Rx(+67.5°) for north panel → normal [0,-0.924,0.383] faces south-upward, reflects overhead sun [0,-0.707,-0.707] onto pot

## panel_cooker.json — new
- CooKit-inspired 4-panel flat reflector cooker (no enclosure)
- Geometry rule: panel at lateral distance d tilted at 67.5°, center_z = d → reflected ray hits z=0 at pot
- back, front flap, left wing, right wing

## No C++ changes needed — plane + real_mirror already supported
