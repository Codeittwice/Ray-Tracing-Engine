# Wave 3 — what goes in the material and component libraries

Contents specification for the two libraries in Wave 3 of `v2_optical_simulator.md`. The plan says
*build a library*; this says *of what*.

Read `v2_optical_simulator.md` § Wave 3 first for the seams and the file list. This document is the
payload: which entries, with which numbers, and which of them the engine cannot express yet.

---

## How to read the numbers

**Every figure here is a single scalar because the engine is monochromatic.** `core::Ray` carries
one `wavelength_nm`, `FluxAccumulator` bins one scalar power, and no source has a spectrum. So a
gold mirror's "R = 0.98" is a statement about the infrared and a lie about the blue, and a
dispersing prism only disperses if you trace it twice at two wavelengths.

Where a value genuinely depends on wavelength, this document says so and gives the band the figure
belongs to. Do not average them into one number silently.

Confidence is marked:

- **[spec]** — a catalogue or standards figure (Schott, Thorlabs, a published constant). Reliable.
- **[typical]** — a representative value for a class of thing that varies by grade, coating batch,
  or how well it was made. Fine as a library default, wrong as a citation.
- **[blocked]** — cannot be represented by the engine today. The gap is named in § Engine gaps.

---

## What the engine can represent today

Before proposing a library entry, check it against this. Wave 3 adds to both lists, but everything
else in the library has to be built from what is here.

**Material types** (`src/io/SceneLoader.cpp:129-176`, allowed keys in `SceneDocument.cpp:69-75`):

| Type | Parameters | Behaviour |
|---|---|---|
| `perfect_mirror` | *(none)* | Specular, R = 1 exactly. |
| `real_mirror` | `reflectance`, `slope_error_mrad` | Specular with a Gaussian normal perturbation. Not a BRDF. |
| `absorber` | *(none)* | Absorbs everything. **No partial absorption.** |
| `dielectric` | `n`, `absorption_per_m`, `sellmeier`, `alpha_spectrum` | Refraction + exact unpolarised Fresnel, returns `Split`. Beer–Lambert on exit. |
| `thin_dielectric_pane` | `n`, `thickness_m`, `absorption_per_m` | Analytic slab R/T. Transmitted ray keeps its direction — no bending, no lateral offset. |

**Surface types** (`SceneDocument.cpp:116-221`): `plane`, `sphere`, `paraboloid`,
`cylindrical_paraboloid`, `quadric`, `fresnel_zone_lens`, `mesh`.

Two facts that constrain half of this document:

1. **`fresnel_zone_lens` is not a lens body.** It is a flat plane at z = 0 whose per-zone normals
   are fictitious, tilted so a paraxial on-axis ray refracts toward the focus. Zero thickness, one
   refracting event. It cannot stand in for a real lens.
2. **`Scene::intersect` has no notion of which medium a ray is currently inside.** `Dielectric`
   infers it per hit from `h.front_face`. So a closed glass body works, and **two overlapping
   dielectric bodies are undefined**. Cemented doublets and beam-splitter cubes must be built from
   disjoint bodies that touch, not overlap.

---

## Part 1 — The material library

### 1.1 Reflective — specular

All `real_mirror`. `slope_error_mrad` is the 1-σ Gaussian normal perturbation; a reflected ray
leaves at roughly **twice** the local tilt, so the error is doubled by the time light arrives.

| Library name | `reflectance` | `slope_error_mrad` | Notes |
|---|---|---|---|
| Kitchen aluminium foil, glued flat | 0.85 | 6.0 | **[typical]** The project's existing default. Real foil on cardboard is worse than this if it wrinkles; 4 for careful work, 10 for a first attempt. |
| Polished aluminium sheet | 0.90 | 3.0 | **[typical]** |
| Anodised reflector sheet (Alanod MIRO type) | 0.93 | 1.5 | **[typical]** Manufacturer figures vary 0.90–0.95 by grade and by whether the number is total or specular solar reflectance. |
| Back-silvered glass mirror | 0.94 | 2.0 | **[typical]** Solar-weighted. A household mirror is around here; a solar-grade one reaches 0.95. |
| Protected silver, laboratory | 0.975 | 0.5 | **[spec]** > 0.97 average across roughly 450–2000 nm. Tarnishes if the coating is scratched. |
| Protected aluminium, laboratory | 0.90 | 0.5 | **[spec]** ≈ 0.90 average 450–2000 nm; the workhorse general-purpose lab mirror, usable into the UV. |
| Protected gold | 0.97 | 0.5 | **[spec]** **Infrared only.** > 0.96 above ~800 nm, and it collapses in the blue — around 0.35 at 400 nm. Meaningless as one number unless the scene is IR; say so in its tooltip. |
| Dielectric laser-line mirror | 0.999 | 0.2 | **[spec]** At the design wavelength and angle only. Away from either it is close to a plain glass plate. Give it a name that carries its wavelength, e.g. "Dielectric mirror, 633 nm, 45°". |
| Ideal mirror | — | — | `perfect_mirror`. A reference for sanity checks, not a material. Label it as such so nobody models a real cooker with it. |

### 1.2 Absorbing

**This whole group is [blocked] except the ideal case.** `absorber` takes no parameters and
absorbs everything, so "black paint, α = 0.95" is not expressible — it would absorb 100%. See
§ Engine gaps G1.

| Library name | Wanted absorptance | Notes |
|---|---|---|
| Ideal black | 1.00 | `absorber` today. Correct for a receiver whose job is to measure what arrives. |
| Matte black stove paint | 0.96 | **[typical]** The realistic cooking-pot surface. |
| Black anodised aluminium | 0.95 | **[typical]** The realistic lab-component surface. |
| Cast iron / enamelled pot | 0.92 | **[typical]** |
| Beam dump | 0.999 | **[spec]** A geometric light trap; the number is the trap, not the coating. |

Until G1 lands, ship only "Ideal black" and say in the panel that partial absorption is not yet
modelled. Shipping a 0.95 entry that silently behaves as 1.00 is worse than not shipping it.

### 1.3 Transmissive

All `dielectric`. `n` is at the sodium d-line (587.6 nm) unless noted. `absorption_per_m` is
Beer–Lambert; the surviving fraction through thickness *t* is exp(−α·t).

| Library name | `n` | `absorption_per_m` | Sellmeier | Notes |
|---|---|---|---|---|
| N-BK7 borosilicate crown | 1.5168 | 0.1 | `bk7` **available** | **[spec]** Abbe 64.2. The default lab glass. |
| Fused silica | 1.4585 | 0.05 | `fused_silica` **available** | **[spec]** Abbe 67.8. UV-transparent, low thermal expansion. |
| N-SF11 dense flint | 1.7847 | 0.5 | **[blocked]** G2 | **[spec]** Abbe 25.7 — low Abbe means strong dispersion, which is exactly why it is the prism glass. Useless for a dispersion demo until its Sellmeier coefficients are added. |
| PMMA (acrylic) | 1.4917 | 1.0 | **[blocked]** G2 | **[typical]** 1.489–1.492 depending on grade. The cheap glazing and cheap-lens material. |
| Polycarbonate | 1.5855 | 4.0 | **[blocked]** G2 | **[typical]** Tougher than acrylic, yellower and more absorbing. |
| Soda-lime window glass | 1.52 | 15.0 | **[blocked]** G2 | **[typical]** The green edge you see on a pane is iron absorption; α varies hugely with iron content. |
| Low-iron "solar" glass | 1.52 | 4.0 | **[blocked]** G2 | **[typical]** The reason solar glazing is specified as low-iron. |
| Water | 1.3330 | 5.0 | **[blocked]** G2 | **[spec]** n at 589 nm, 20 °C. For a water-filled lens or a solar still. |

`thin_dielectric_pane` variants of the glazings (window glass, low-iron, acrylic) are worth
shipping separately and labelling clearly: the pane model is an **analytic slab** that does not
bend the transmitted ray at all. Correct and much cheaper for a flat cooker lid; wrong for anything
where refraction is the point.

### 1.4 Diffusing — **needs the new `diffuser` material**

`real_mirror`'s slope error perturbs a normal, which is a narrow lobe around the specular
direction. It cannot produce Lambertian scattering, so every entry here needs the new type.

| Library name | Model | `reflectance` | Notes |
|---|---|---|---|
| Spectralon / PTFE white standard | Lambertian | 0.99 | **[spec]** ≈ 0.99 across 400–1500 nm, near-perfectly Lambertian. The calibration reference. |
| Matte white paint | Lambertian | 0.85 | **[typical]** |
| White card / paper screen | Lambertian | 0.80 | **[typical]** The thing you actually hold in a beam to see it. |
| Ground glass diffuser | Transmissive, Gaussian lobe | — | **[typical]** Scatters in *transmission*, not reflection. Grit number sets the lobe width: 220-grit is wide, 1500-grit narrow. A different model again from the reflective entries. |
| Brushed aluminium | Gaussian lobe | 0.85 | **[typical]** The in-between case: a broad specular lobe, not a mirror and not Lambertian. |

Start with pure Lambertian reflection (cosine-weighted hemisphere sample). The Gaussian-lobe and
transmissive-diffuser cases are a second step — say which is implemented in the material's tooltip
rather than letting the name imply more than the model does.

### 1.5 Splitting — **needs the new `beam_splitter` material**

Every split ratio in the engine today is *derived* from Fresnel, so a 50:50 splitter can only be
faked by solving for the index that gives R = 0.5 at one angle — and it drifts everywhere else.
`InteractionKind::Split` already carries both branches; what is missing is a *designed* ratio.

| Library name | R : T | Notes |
|---|---|---|
| 50:50 plate beamsplitter | 50 : 50 | The interferometer workhorse. |
| 50:50 cube beamsplitter | 50 : 50 | Same ratio, different geometry — the body is a cube with the coating on the internal diagonal. |
| 90:10 pickoff | 10 : 90 | For sampling a beam to monitor it without spending it. |
| 70:30, 30:70 | as named | |
| Pellicle beamsplitter | 45 : 55 | **[typical]** A ~2 µm nitrocellulose membrane. Its point is that it produces no ghost reflection and no beam displacement, which the engine gets for free — a pellicle is the one splitter a zero-thickness plane models honestly. |
| Polarising beamsplitter (PBS) | — | **[blocked]** Needs Wave 5's polarisation. Transmits p, reflects s. Do not ship a fake one. |
| Dichroic / longpass | — | **[blocked]** Wavelength-dependent by definition; needs a spectrum. |

Parameters: `reflectance` (the fraction reflected) and `absorptance` (the fraction lost in the
coating — real splitters are not lossless; 1–5% is typical). R + T + A = 1.

---

## Part 2 — The component library

Each entry is an `ElementDoc` template: a surface, a material binding, and a transform, dropped at a
chosen point. Sizes follow the two conventions actual hardware uses — **imperial optics are 1" =
25.4 mm and 2" = 50.8 mm diameter**; solar components are whatever the builder cut.

### 2.1 Sources

| Component | Parameters | Notes |
|---|---|---|
| Sun | direction or azimuth/elevation, DNI, sunshape | Exists. Owns its aperture since Wave 2. |
| HeNe laser | 632.8 nm, 0.5–5 mW, beam Ø 0.8 mm, divergence 1.3 mrad | **[spec]** wavelength; **[typical]** the rest. The classic red bench laser. Good default. `sources::Laser` exists since Wave 2 — note its divergence parameter is the **full** angle in mrad, not the half angle. |
| Green DPSS laser | 532 nm, 1–100 mW, Ø 1.5 mm, 1.5 mrad | **[spec]** wavelength. Brightest to the eye — good for screenshots. |
| Red diode laser | 650 nm, 1–5 mW | **[typical]** |
| Violet diode laser | 405 nm, 1–50 mW | **[typical]** Shows dispersion most strongly against 633. |
| IR diode | 780 nm or 1064 nm | **[typical]** Pair with the gold mirror. |
| Collimated lamp | broadband, watts | Needs a new `LightSource` subclass — the general source arrived in Wave 2, but only `sun` and `laser` exist. No spectrum either (G6), so it is a monochromatic stand-in; label it as one. |

### 2.2 Mirrors

| Component | Surface | Suggested sizes |
|---|---|---|
| Flat mirror, round | `plane` + a circular clip *(see G3)* | Ø 25.4, 50.8 mm |
| Flat mirror, square | `plane` | 25 × 25, 50 × 50 mm |
| Concave spherical mirror | `sphere`, clipped | R = 2f; f = 50, 100, 200, 500 mm |
| Concave parabolic mirror | `paraboloid` | f = 50–1000 mm. **On-axis only** — the engine has no off-axis parabola, and an OAP is the standard lab component. See G4. |
| Solar dish | `paraboloid` | f/D 0.4–0.6, Ø 0.5–2 m |
| Solar trough | `cylindrical_paraboloid` | f 0.2–0.5 m, length 1–3 m |
| Flat panel reflector | `plane` | The existing cooker reflector. Keep. |

### 2.3 Lenses — **needs the new `thick_lens` surface**

A real lens is two curved refracting interfaces separated by glass. `fresnel_zone_lens` is not one,
and a `sphere` is a closed ball rather than a lens body, so this is genuinely new geometry.

Parameters: `radius1`, `radius2` (signed — positive means the centre of curvature is on the +z
side), `center_thickness_m`, `diameter_m`, plus a `dielectric` material binding.

| Component | R1 / R2 | Notes |
|---|---|---|
| Plano-convex (PCX) | R / ∞ | **The workhorse.** At 1" Ø, stock focal lengths are 25.4, 30, 35, 40, 50, 60, 75, 100, 125, 150, 200, 250, 300, 400, 500, 750, 1000 mm **[spec]**. For N-BK7, f ≈ R / (n−1) ≈ R / 0.5168, so a 50 mm PCX has R ≈ 25.8 mm. |
| Bi-convex (DCX) | R / −R | Better than PCX at 1:1 conjugates. f ≈ R / (2(n−1)). |
| Plano-concave (PCV) | −R / ∞ | Negative f; beam expanders. |
| Bi-concave (DCV) | −R / R | |
| Achromatic doublet | two bodies | Crown + flint cemented. Must be **two disjoint touching bodies** (N-BK7 + N-SF11), never overlapping — see the medium-tracking limit above. Needs N-SF11's Sellmeier, so **[blocked]** on G2 for the achromatic behaviour, though the geometry works now. |
| Cylindrical lens | `cylindrical_paraboloid`-like, refracting | Focuses to a line. Needs its own surface or a clipped quadric. |
| Fresnel lens | `fresnel_zone_lens` | Exists. The solar-cooker lens. |

### 2.4 Prisms and splitters

| Component | Build | Notes |
|---|---|---|
| Equilateral dispersing prism | A triangular `mesh`, or three clipped planes as a closed body | 25–50 mm faces. N-SF11 for a visible spectrum. **[blocked]** on G2 for actual dispersion. |
| Right-angle prism | Same, 90-45-45 | Used as a mirror by TIR — a good test that TIR works, since `Dielectric` already returns full-power `Reflected` on TIR. |
| Beamsplitter plate | `plane` + `beam_splitter` material | Usually used at 45°. A real plate produces a ghost from its second surface, which a zero-thickness plane will not show — note it. |
| Beamsplitter cube | Two glass prisms + a coated internal diagonal | Disjoint touching bodies. The honest version of a cube. |
| Pellicle beamsplitter | `plane` + `beam_splitter` | The one splitter a zero-thickness plane models correctly. |

### 2.5 Apertures, screens and detectors

| Component | Build | Notes |
|---|---|---|
| Iris / aperture stop | An absorbing annulus | **[blocked]** on G3 — needs an annular or holed plane. Central to Wave 6, since diffraction through a circular aperture is the Airy demo. |
| Single slit | Absorbing plane with a slot | Same gap. Width 10–200 µm for visible diffraction. |
| Double slit | Two slots | Separation 0.1–1 mm. |
| Screen / detector card | `plane` + receiver | Exists as the receiver. Rename it in bench scenes — "screen", not "pot". |
| Power meter head | Small `plane` receiver | Ø 10–20 mm. Reports total watts, which the accumulator already gives. |
| Cooking pot | `plane` or box receiver | Exists. Keep. |

### 2.6 Furniture — decorative, no optics

These must not participate in tracing. They are display-only geometry, and **they will be folded
into Polyscope's global `lengthScale` and `boundingBox` unless extents are pinned** — see Wave 4's
note, and the three bug comments in the codebase about what that costs.

| Component | Notes |
|---|---|
| Optical breadboard / table | Metric: M6 holes on a 25 mm grid. Imperial: 1/4"-20 on a 1" grid. **[spec]** Both are worth offering — the hole grid is what makes a bench picture legible, and it doubles as the snap grid in Wave 4. |
| Post and post holder | Ø 12.7 mm (½") posts **[spec]**. Sets the standard beam height: 3" or 4" above the table. |
| Kinematic mirror mount | The thing a lab mirror actually sits in. |
| Lens mount / cage plate | 30 mm cage system **[spec]**. |

---

## Engine gaps this library depends on

Each of these blocks library entries above. Numbered so entries can cite them.

- **G1 — `absorber` cannot absorb partially.** It takes no parameters. Every realistic absorbing
  surface in § 1.2 needs either a `reflectance` parameter on `absorber` (which changes an existing
  material type and therefore needs a golden-value gate) or a new `diffuse_absorber` type. The
  second is safer.
- **G2 — only two Sellmeier presets exist**, `bk7` and `fused_silica` (`Dielectric.hpp:19-27`,
  validated in `SceneDocument.cpp`). N-SF11, PMMA, polycarbonate, soda-lime and water all need
  coefficients adding. Without N-SF11 there is no convincing prism. Note that adding presets is
  additive and touches the strict-mode validator's allow-list.
- **G3 — there is no circular or annular plane.** `plane` is a rectangle. Round optics, irises and
  slits all need either a clip region on `plane` or a new surface. This is the single most
  load-bearing gap in the list: it blocks every round lab component *and* the Wave 6 diffraction
  demos.
- **G4 — no off-axis parabola.** `paraboloid` is on-axis. An OAP is standard lab hardware and the
  usual way to focus without blocking the beam. Possibly expressible as a clipped `quadric`; worth
  checking before writing a new surface.
- **G5 — no medium tracking.** `Dielectric` infers the medium from `h.front_face` per hit, so
  overlapping dielectric bodies are undefined. Cemented doublets and splitter cubes must be
  disjoint bodies that touch. Document this in the library entries, since a user dragging two
  lenses together will hit it.
- **G6 — no spectrum anywhere.** One wavelength per ray, no spectral weighting in the accumulator.
  Dichroics, colour, and any "broadband" figure are approximations. Wave 5's dispersion demos work
  by tracing twice at two wavelengths, not by tracing white light.

---

## Suggested shipping order

1. **Materials that need no engine change**: the whole of § 1.1 (reflective) and the two
   Sellmeier-backed entries in § 1.3. Immediately useful, zero risk to the golden values.
2. **`beam_splitter` material** (§ 1.5, the non-blocked rows). `Split` already exists and the
   tracer already handles it, so this is a new material type and nothing else.
3. **G3, the circular/annular clip.** It unblocks the most and is needed by Wave 6 regardless.
4. **`thick_lens` surface** (§ 2.3) and the lens components.
5. **`diffuser` material** (§ 1.4, Lambertian only).
6. **G1 and G2**, which turn the [blocked] realistic entries on.
7. **Furniture** (§ 2.6), which is Wave 4's geometry work as much as Wave 3's.

Ship an entry only when the engine can honour it. A library row whose behaviour does not match its
name is worse than a missing row — the user cannot tell, and the number they get is wrong in a way
that looks authoritative.
