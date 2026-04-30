# Box and Panel Solar Cookers — Technical Reference

A consolidated reference of components, geometry, angles, materials, and performance numbers from established designs. Purpose: provide enough detail to specify a model (e.g. ray-tracing) or build prototype.

---

## 1. PANEL COOKERS

Panel cookers are flat reflective sheets (cardboard + aluminum foil or mirror film) folded around a central black pot inside a transparent enclosure. No focal point — they spread reflected sun across the pot.

### 1.1 CooKit (Solar Cookers International, public-domain reference design)

The benchmark panel cooker. All dimensions derive from a 48 in × 36 in (≈122 × 91 cm) sheet of cardboard, or in metric, **128 × 96 cm** using a 4 cm grid unit.

**Components:**
- Single sheet of corrugated cardboard, ~0.9 × 1.2 m
- Aluminum foil (~0.3 × 3 m), glued shiny-side-out with diluted (1:1) water-based glue
- Black cooking pot
- High-temp oven bag (rated to ~315 °C / 600 °F) — or a glass/Pyrex enclosure for a reusable alternative
- Pot stand (small wire trivet)

**Geometry (folded):**
- A flat horizontal base under the pot
- A tall back reflector (vertical-ish)
- Two angled side wings
- A short front flap that is propped up at an adjustable angle to bounce extra light onto the pot
- Slots cut so the front locks into the back panel — no glue needed at the joints

**Operating angles:**
- Tilt the front flap so it casts a shadow under itself no wider than half the flap's width — this is the rule of thumb used in the SCI manual. Higher sun → higher flap angle; lower sun → lower flap angle.
- Whole cooker faces ~SE in the morning for a noon meal, S at noon, etc. (Northern hemisphere)
- No tracking required mid-cook for short cooks; reposition every 1–2 hours for long cooks.

**Performance:**
- Stagnation temperatures around **~120–150 °C** in good sun
- Delivered power measured at ~150 W to 1 L of water (UK test, ~50°N) — comparable to a low-setting electric slow-cooker
- Reaches and holds boiling

**Reflector geometry rule of thumb (applies to box and panel both):** Only the ray hitting the *top edge* of a flat reflector matters — if that ray lands on the far edge of the target (the pot/absorber), every ray below it also lands on target. This is the key relation for sizing reflectors.

For a flat reflector of length L tilted at angle θ above a target of length T, the geometry that puts the top-edge ray on the far side of the target is:
- L · sin(θ – α) reaches across T, where α is the sun's elevation above the cooker plane
- Equivalently, with sun directly overhead and a horizontal target: optimal flat-reflector tilt ≈ 60° gives a good balance for most sun angles between 45° and 90°

### 1.2 Roger Bernard panel cooker (the original, predecessor to the CooKit)

- Made from a single rectangular cardboard box
- Bottom footprint: ~23 × 25 cm
- Height: ~30 cm
- Weight: <1 kg
- Single-person cooking

### 1.3 Fun-Panel (Teong Tan)

A more compact, more adjustable variant. Built from **half of a single 50 × 50 × 50 cm cardboard box**.

- Reflecting surface based on a 60° conical/funnel geometry (steep walls form a partial cone around the pot) — that 60° is the same angle as the "Funnel Cooker" it derives from
- Two large rectangular cardboard panels joined together
- Scales up: 60×60 or 65×65 cm for more cooking power
- Multi-position: rectangular panel on floor for low sun (35–50°); flip onto square panel resting on a 12–15 cm high support box for high sun (50–70°); tilt panels back for very high sun (>70°)

### 1.4 SunGood (Solar Brother, commercial)

DIY kit dimensions:
- Full template **140 × 100 cm** for the cardboard body
- 8 separate mirror-film reflector pieces cut from a matching pattern
- Folds flat in 1 minute
- Reaches up to **120 °C**
- Two preset positions (winter/summer) for optimal angle by season

### 1.5 Materials notes (panel cookers in general)

- **Reflector surface**: aluminum foil works (cheap, widely available, ~85–90% reflectivity); flexible mirror film works better (~94%); rigid mirror tiles best but heavy and fragile. The Bahir Dar paper measured a clear performance jump going from foil to mirror glass.
- **Substrate**: corrugated cardboard is the standard. Plywood (1/4 in) or polypropylene flute board for durability — Poly-Furnace was a polypropylene CooKit shipped to Sudan for refugee camps because cardboard versions died in <12 months.
- **Glazing**: oven bag (one-shot) reaches ~315 °C without melting. Doubling the bag (one inside another) raises pot temperature ~20 °C. Reusable alternative: a glass bowl, Pyrex dish, or transparent acrylic box around the pot.
- **Pot**: must be dark/matte black, thin-walled (low thermal mass = faster heat-up), with a tight lid.

---

## 2. BOX COOKERS

A box cooker is an insulated, glazed enclosure. The box itself is the oven; reflectors merely boost input. They reach higher and more stable temperatures than panel cookers because the glazing creates a greenhouse trap.

### 2.1 Reference design — Milikias et al. / AIMS Energy 2018

This is a well-documented research prototype with explicit dimensions:

**Outer box:** 600 × 600 × 250 mm
**Inner cooking chamber:** pyramidal/trapezoidal — 470 × 470 mm at the top, 300 × 300 mm at the bottom, **150 mm depth**
**Insulation gap:** 65 mm at top, 85 mm at bottom (compressed sawdust, 50 mm thick layer)
**Inner box wall:** 1.5 mm iron sheet, painted matt black inside
**Outer box wall:** 50 mm single-walled cardboard
**Cooking volume:** 0.0306 m³

**Glazing:**
- Two clear window-glass panes, 4 mm thick, 500 × 500 mm each
- **20 mm air gap** between the two panes (this gap is critical — it's what gives double-glazing its insulation)
- Mounted in an openable wooden frame that hinges to one side

**Reflectors:** three flat panels
- Each is 500 × 500 mm
- 10 mm cardboard backing covered with thin aluminum foil
- Adjustable hinges; track sun every 15–30 minutes during cooking
- Mounted on three sides of the box

**Performance:**
- Figure of Merit F1 = 0.123 K·m²/W (a measure of optical efficiency × insulation)
- Standard cooking power P50 = 36 W (51 W with vapor wiper to clear condensation)
- Cumulative efficiency 22%

### 2.2 Production box cooker (commercial 4-pot type, India)

- External: 480 × 480 × 170 mm
- Mass: 13.5 kg (powder-coated steel)
- Holds 4 aluminum cooking pots
- Operating range: **120–140 °C**
- Insulation R-value: 0.96 m²·°C/W

### 2.3 Optimized sidewall-angled cooker (Jain et al. / J. Therm. Eng. 2023)

This is the design where the inner walls themselves act as reflectors instead of being vertical. From a Surat (India) optimization, optimal sidewall tilts (from vertical, with cooker facing south) for December insolation:
- South-facing wall: **67.30°**
- North-facing wall: **22.69°**
- East-facing wall: **35.44°**
- West-facing wall: equal to East side (mirror symmetric)

These angles convert otherwise wasted wall area into solar collection area at low winter sun.

Outer panel material: 500 × 500 mm aluminum composite panel (ACP). Glazing: two 4 mm float glass panes with 15 mm air gap. (Note this 15 mm vs the 20 mm above — the optimal gap is in the 15–20 mm range; below ~10 mm conduction dominates, above ~25 mm convection currents start.)

### 2.4 Design rules from the SCI design principles document

- **Depth of inner box: 10–15 cm max** (Kundapur). Deeper boxes shadow themselves; the width and length can be anything but depth is bounded.
- **Aperture-to-loss-area ratio**: maximize. Bigger glazing relative to wall area = hotter cooker.
- **Shape**: for cookers facing noon sun, make the **east–west dimension longer than the north–south** so the reflector remains useful as the sun arcs.
- **The more directly the glass faces the sun, the higher the gain.** Tilting the whole cooker toward the sun increases input proportional to cos(angle of incidence on glass).

### 2.5 Reflector tilt angle for box cookers (Algifri & Al-Towaie, Aden)

For a single flat reflector mounted on the back edge of a box cooker glazing, with the cooker oriented so the surface azimuth angle γ = 0 (reflector pointing directly at the sun in azimuth), the optimum geometric relation is:

**3·R − 2·α = 180°**

where:
- R = reflector tilt angle (from the glazing plane)
- α = solar elevation angle

Equivalently: **R = (180° + 2α) / 3**

Examples at different sun elevations:
- α = 30° → R = 80°
- α = 45° → R = 90°
- α = 60° → R = 100°
- α = 75° → R = 110° (i.e. 20° past vertical, leaning over the box)

This gives >100% improvement on cooker temperature at low winter elevations and >60% at high elevations vs. no reflector.

### 2.6 Booster-mirror angle (Sengar et al., 30°N latitude)

Separate mathematical relations for two cases:
- λ = optimum booster tilt for a horizontally-placed cooker
- ψ = optimum booster tilt for a cooker tilted at the optimum angle (latitude-tilted)

Both depend on the day of the year (solar declination). The takeaway is that a *fixed* optimum tilt for the year is achievable for large-aspect-ratio cookers with dual boosters — small cookers benefit from hourly adjustment.

### 2.7 Materials checklist for a box cooker

| Component | Standard option | Better option |
|---|---|---|
| Outer box | Corrugated cardboard 50 mm | Plywood / sheet metal |
| Inner box | Cardboard + aluminum foil | 1.5 mm iron sheet, matt black |
| Insulation | Sawdust / thermocol (EPS, 50 mm) | Mineral wool / multi-layer wool + bubble foil |
| Glazing | Single 4 mm acrylic / plastic | **Double 4 mm glass with 15–20 mm air gap** |
| Reflector | Aluminum foil on cardboard | Mirror glass / reflective film (94% reflectivity) |
| Absorber | Black-painted cardboard or plate | Matt-black-coated aluminum/iron sheet |
| Pot | Dark thin-walled aluminum | Black-anodized with tight lid |
| Sealing | Tape / silicone | EPDM gasket on hinged frame |

### 2.8 Achievable performance

| Configuration | Stagnation temperature |
|---|---|
| Cardboard + foil + plastic glazing, no reflector | 70–90 °C |
| Cardboard + foil + single glass + 1 reflector | 100–120 °C |
| Insulated + double glass + 3 reflectors (Milikias) | ~150 °C |
| Optimized sidewall + double glass + booster (Jain) | up to ~165 °C |
| Conventional commercial cooker (Wikipedia upper bound) | **165 °C** |

Solar cookers as a category target 65 °C (baking) up to 400 °C (searing); box cookers fall in the lower part of this range.

---

## 3. KEY PHYSICS AND EQUATIONS (for modeling)

**Greenhouse trap:** Glass transmits ~90% of incoming shortwave (visible/near-IR) sunlight but is opaque to the longwave thermal IR re-emitted by the hot black absorber. Low-iron glass is preferred — standard glass absorbs more in the near-IR.

**Heat balance at stagnation (no load):**
Q_in = Q_out
η_opt · I · A_aperture = U · A_loss · (T_pot − T_ambient)

So stagnation T = T_ambient + (η_opt · I · A_aperture) / (U · A_loss)

- I = direct solar irradiance (W/m²); ~800–1000 W/m² at midday
- η_opt = optical efficiency (transmission · absorptivity · reflector geometry factor); ~0.5–0.7 for a good box cooker
- A_aperture = glazing area (m²)
- A_loss = sum of wall areas weighted by their U-values (m²)
- U = overall heat-loss coefficient (W/m²·K); ~3–6 W/m²·K for a single-glazed box, ~1.5–3 for double-glazed insulated

**Cooking power (Funk and Larson, ASAE standard):** P = m·c·ΔT/Δt, normalized to 700 W/m² insolation and 50 °C temperature differential = "P50". Decent box cooker: 30–60 W. CooKit-class panel: ~150 W delivered to water at low ΔT but tails off near boiling.

**Reflector contribution:** For a flat reflector of area A_r reflecting onto a glazing of area A_g at sun elevation α and reflector tilt R, the effective concentration factor is roughly:
C ≈ 1 + ρ · (A_r / A_g) · cos(2R − α − 90°)
where ρ is reflectivity (~0.85–0.94). The angular term is what the 3R − 2α = 180° rule maximizes.

**Snell / thin-lens (only relevant if a Fresnel cover is used over the box — not standard):** included for completeness only; not needed for pure box or panel cookers.

---

## 4. RECOMMENDATIONS FOR THE TU/e PROJECT (Lelystad / Eindhoven, ~52°N)

Given the high latitude and cool climate:
- **Sun elevation at solar noon at 52°N:** ranges from ~14.5° at winter solstice to ~61° at summer solstice. For a project tested in spring/early summer, expect 40–55° at noon.
- A simple panel cooker at this latitude reaches boiling on clear days but struggles on hazy ones — Solar Brother specifically calls this out as a SunGood limitation needing the "winter" preset position.
- A box cooker with double glazing and 3 reflectors will outperform a panel cooker meaningfully here. The Bahir Dar mirror-glass result showed measurable gains.
- For the C2C / sustainability constraint flagged in your SSA: replace the single-use oven bag with a glass bowl or a reusable acrylic dome over the pot. Cardboard outer body and EPS insulation are recyclable; aluminum foil and mirror film are recyclable; double-glass glazing is reusable indefinitely.
- For portability: target the CooKit-scale (≤1.2 m largest dimension, foldable, <2 kg).

**Suggested baseline geometry to model (compromise between panel CooKit and box):**
- 500 × 500 mm aperture
- 150 mm deep cooking chamber
- 50 mm thermocol insulation on all walls (excl. aperture)
- Matt-black 4 mm aluminum absorber plate
- Double 4 mm glass glazing, 18 mm air gap
- 3 hinged flat aluminum-foil-on-cardboard reflectors, 500 × 500 mm each
- Reflector tilt set per (180° + 2α)/3 — adjustable hinge

This is small enough to be portable, follows the well-validated Milikias dimensions, but uses Jain's sidewall geometry tweak as an optional upgrade.

---

## 5. SOURCES

- Solar Cookers International — CooKit handbook and template (public domain)
- Solar Cooking Wiki — "Principles of Solar Box Cooker Design", "CooKit", "Fun-Panel", "Solar reflector design", "Solar Panel Bernard"
- Milikias et al., AIMS Energy 6(1), 2018 — box cooker prototype 600×600×250 mm
- Jain et al., J. Therm. Eng. 9(3), 2023 — optimized sidewall angles
- Algifri & Al-Towaie, Solar Energy 2001 — 3R − 2α = 180° rule
- Sengar et al., on Booster mirror inclination angles, 30°N
- Bahir Dar performance evaluation (PMC) — foil vs. mirror glass reflectors
- Solar Brother SunGood DIY kit specs
- Wikipedia, "Solar cooker" — temperature ranges and history
- Ashok Kundapur — depth-of-cooker rule
- Funk & Larson 1998 — cooking power standard
