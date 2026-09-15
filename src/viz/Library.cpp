#include "scrt/viz/Library.hpp"
#include "scrt/viz/AppConfig.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace scrt::viz {

namespace {

using json = nlohmann::json;

io::MaterialDoc mat(const char* type, json params) {
    io::MaterialDoc m;
    m.type   = type;
    m.params = std::move(params);
    return m;
}

MaterialEntry entry(const char* name, const char* group, const char* note, io::MaterialDoc d) {
    MaterialEntry e;
    e.name  = name;
    e.group = group;
    e.note  = note;
    e.doc   = std::move(d);
    e.doc.id = e.name;
    return e;
}

} // namespace

const std::vector<MaterialEntry>& builtin_materials() {
    static const std::vector<MaterialEntry> v = [] {
        std::vector<MaterialEntry> m;

        // ---- Mirrors: specular, real_mirror. slope_error_mrad is the 1-sigma normal
        // perturbation, and a reflected ray leaves at roughly TWICE the local tilt.
        m.push_back(entry("Kitchen aluminium foil", "Mirrors",
                          "Typical, not a catalogue figure. Foil glued flat on board. Slope "
                          "error 4 mrad for careful work, 10 for a first attempt.",
                          mat("real_mirror", {{"reflectance", 0.85}, {"slope_error_mrad", 6.0}})));
        m.push_back(entry("Polished aluminium sheet", "Mirrors", "Typical.",
                          mat("real_mirror", {{"reflectance", 0.90}, {"slope_error_mrad", 3.0}})));
        m.push_back(entry("Anodised reflector sheet", "Mirrors",
                          "Typical. Manufacturer figures for this class run 0.90 to 0.95 "
                          "depending on grade, and on whether the number quoted is total or "
                          "specular solar reflectance.",
                          mat("real_mirror", {{"reflectance", 0.93}, {"slope_error_mrad", 1.5}})));
        m.push_back(entry("Back-silvered glass mirror", "Mirrors",
                          "Typical, solar-weighted. A household mirror is about here; a "
                          "solar-grade one reaches 0.95.",
                          mat("real_mirror", {{"reflectance", 0.94}, {"slope_error_mrad", 2.0}})));
        m.push_back(entry("Protected silver, laboratory", "Mirrors",
                          "Catalogue: above 0.97 average across roughly 450 to 2000 nm. "
                          "Tarnishes if the coating is scratched.",
                          mat("real_mirror", {{"reflectance", 0.975}, {"slope_error_mrad", 0.5}})));
        m.push_back(entry("Protected aluminium, laboratory", "Mirrors",
                          "Catalogue: about 0.90 average 450 to 2000 nm. The general-purpose "
                          "lab mirror, usable into the ultraviolet.",
                          mat("real_mirror", {{"reflectance", 0.90}, {"slope_error_mrad", 0.5}})));
        m.push_back(entry("Protected gold (infrared only)", "Mirrors",
                          "Catalogue, ABOVE ~800 nm ONLY: better than 0.96 in the infrared and "
                          "around 0.35 at 400 nm. This engine traces one wavelength per ray "
                          "with no spectrum, so this single number is a lie anywhere but the "
                          "infrared. Use it only in an infrared scene.",
                          mat("real_mirror", {{"reflectance", 0.97}, {"slope_error_mrad", 0.5}})));
        m.push_back(entry("Dielectric laser mirror (design wavelength)", "Mirrors",
                          "Catalogue, AT ITS DESIGN WAVELENGTH AND ANGLE ONLY, typically 633 nm "
                          "at 45 degrees. Away from either it behaves like a plain glass plate, "
                          "which this entry does not model.",
                          mat("real_mirror", {{"reflectance", 0.999}, {"slope_error_mrad", 0.2}})));
        m.push_back(entry("Ideal mirror (reference)", "Mirrors",
                          "Reflects everything, perfectly specular. A sanity-check reference, "
                          "not a material: no real surface behaves like this.",
                          mat("perfect_mirror", json::object())));

        // ---- Glass: dielectric. n at the sodium d line (587.6 nm) unless noted.
        m.push_back(entry("N-BK7 borosilicate crown", "Glass",
                          "Catalogue. Abbe 64.2, the default lab glass. Dispersion is modelled "
                          "(Sellmeier), so tracing at two wavelengths gives two indices.",
                          mat("dielectric", {{"n", 1.5168}, {"absorption_per_m", 0.1},
                                             {"sellmeier", "bk7"}})));
        m.push_back(entry("Fused silica", "Glass",
                          "Catalogue. Abbe 67.8, transparent into the ultraviolet, low thermal "
                          "expansion. Dispersion modelled.",
                          mat("dielectric", {{"n", 1.4585}, {"absorption_per_m", 0.05},
                                             {"sellmeier", "fused_silica"}})));
        m.push_back(entry("N-SF11 dense flint", "Glass",
                          "Catalogue. Abbe 25.7 - strongly dispersive, which is exactly why it "
                          "is the prism glass. Dispersion modelled.",
                          mat("dielectric", {{"n", 1.7847}, {"absorption_per_m", 0.5},
                                             {"sellmeier", "n_sf11"}})));
        m.push_back(entry("PMMA (acrylic)", "Glass",
                          "Typical; grades run 1.489 to 1.492. The cheap glazing and cheap-lens "
                          "material. Dispersion modelled.",
                          mat("dielectric", {{"n", 1.4917}, {"absorption_per_m", 1.0},
                                             {"sellmeier", "pmma"}})));
        m.push_back(entry("Polycarbonate", "Glass",
                          "Typical. Tougher than acrylic, yellower and more absorbing. "
                          "Dispersion modelled.",
                          mat("dielectric", {{"n", 1.5855}, {"absorption_per_m", 4.0},
                                             {"sellmeier", "polycarbonate"}})));
        m.push_back(entry("Soda-lime window glass (no dispersion)", "Glass",
                          "Typical. NO dispersion data: this entry has a fixed index, so it "
                          "will not disperse however many wavelengths you trace. The green edge "
                          "on a real pane is iron absorption, and the absorption figure varies "
                          "hugely with iron content.",
                          mat("dielectric", {{"n", 1.52}, {"absorption_per_m", 15.0}})));
        m.push_back(entry("Low-iron solar glass (no dispersion)", "Glass",
                          "Typical, fixed index as above. The low absorption is the reason "
                          "solar glazing is specified as low-iron.",
                          mat("dielectric", {{"n", 1.52}, {"absorption_per_m", 4.0}})));
        m.push_back(entry("Water (no dispersion)", "Glass",
                          "Catalogue index at 589 nm and 20 C, fixed. For a water-filled lens "
                          "or a solar still.",
                          mat("dielectric", {{"n", 1.3330}, {"absorption_per_m", 5.0}})));

        // ---- Glazing panes: the analytic slab. The transmitted ray is NOT bent.
        m.push_back(entry("Glazing pane, low-iron 4 mm", "Glazing",
                          "Analytic slab: reflection and absorption are modelled, but the "
                          "transmitted ray keeps its direction - no bending and no sideways "
                          "offset. Correct and much cheaper for a flat cooker lid; wrong for "
                          "anything where refraction is the point.",
                          mat("thin_dielectric_pane", {{"n", 1.52}, {"thickness_m", 0.004},
                                                       {"absorption_per_m", 4.0}})));
        m.push_back(entry("Glazing pane, acrylic 3 mm", "Glazing",
                          "Analytic slab, as above: no bending of the transmitted ray.",
                          mat("thin_dielectric_pane", {{"n", 1.4917}, {"thickness_m", 0.003},
                                                       {"absorption_per_m", 1.0}})));

        // ---- Matte surfaces: Lambertian diffusers. This is also how a PARTLY absorbing
        // surface is expressed; the ideal absorber below takes no parameters at all.
        m.push_back(entry("Spectralon white standard", "Matte surfaces",
                          "Catalogue: about 0.99 across 400 to 1500 nm and very close to "
                          "Lambertian. The calibration reference.",
                          mat("diffuser", {{"albedo", 0.99}})));
        m.push_back(entry("Matte white paint", "Matte surfaces", "Typical.",
                          mat("diffuser", {{"albedo", 0.85}})));
        m.push_back(entry("White card screen", "Matte surfaces",
                          "Typical. The card you hold in a beam to see where it lands.",
                          mat("diffuser", {{"albedo", 0.80}})));
        m.push_back(entry("Matte black stove paint", "Matte surfaces",
                          "Typical: absorbs about 96% and scatters the rest. The realistic "
                          "cooking-pot surface. Unlike the ideal absorber it does return a few "
                          "percent into the scene, which is what a real black surface does.",
                          mat("diffuser", {{"albedo", 0.04}})));
        m.push_back(entry("Black anodised aluminium", "Matte surfaces",
                          "Typical: the realistic lab-component surface.",
                          mat("diffuser", {{"albedo", 0.05}})));
        m.push_back(entry("Cast iron or enamelled pot", "Matte surfaces", "Typical.",
                          mat("diffuser", {{"albedo", 0.08}})));
        m.push_back(entry("Beam dump", "Matte surfaces",
                          "A geometric light trap. The figure describes the trap, not a "
                          "coating; here it is modelled as a very dark matte surface.",
                          mat("diffuser", {{"albedo", 0.001}})));
        m.push_back(entry("Ideal black (reference)", "Matte surfaces",
                          "Absorbs everything that reaches it and returns nothing. Correct for "
                          "a receiver whose job is to measure what arrives; not a real "
                          "material.",
                          mat("absorber", json::object())));

        // ---- Beam splitters: a DESIGNED ratio held at every angle. Zero thickness, so the
        // transmitted beam is not displaced and there is no second-surface ghost.
        m.push_back(entry("Beamsplitter 50:50", "Beam splitters",
                          "The interferometer workhorse. 2% lost in the coating, which real "
                          "splitters do. Modelled at zero thickness: a real PLATE would also "
                          "produce a faint ghost from its second face, which this does not.",
                          mat("beam_splitter", {{"reflectance", 0.50}, {"absorptance", 0.02}})));
        m.push_back(entry("Beamsplitter 70:30", "Beam splitters", "As above.",
                          mat("beam_splitter", {{"reflectance", 0.70}, {"absorptance", 0.02}})));
        m.push_back(entry("Beamsplitter 30:70", "Beam splitters", "As above.",
                          mat("beam_splitter", {{"reflectance", 0.30}, {"absorptance", 0.02}})));
        m.push_back(entry("Pickoff 90:10", "Beam splitters",
                          "Reflects a tenth, for sampling a beam to monitor it without "
                          "spending it.",
                          mat("beam_splitter", {{"reflectance", 0.10}, {"absorptance", 0.01}})));
        m.push_back(entry("Pellicle beamsplitter 45:55", "Beam splitters",
                          "A membrane about 2 microns thick. Its point is that it makes no "
                          "ghost and no beam displacement - which is exactly what a "
                          "zero-thickness surface models, so this is the one splitter the "
                          "engine represents honestly.",
                          mat("beam_splitter", {{"reflectance", 0.45}, {"absorptance", 0.01}})));
        return m;
    }();
    return v;
}

const std::vector<ComponentEntry>& builtin_components() {
    static const std::vector<ComponentEntry> v = [] {
        std::vector<ComponentEntry> c;
        auto add = [&c](const char* name, const char* group, const char* note,
                        const char* material, io::SurfaceDoc s) {
            ComponentEntry e;
            e.name = name; e.group = group; e.note = note; e.material = material;
            e.surface = std::move(s);
            c.push_back(std::move(e));
        };

        // ---- Mirrors. Imperial optics: 1 inch = 25.4 mm, 2 inch = 50.8 mm.
        add("Flat mirror, round 1 inch", "Mirrors", "25.4 mm diameter, the standard lab size.",
            "Protected aluminium, laboratory", io::DiskDoc{0.0127, 0.0});
        add("Flat mirror, round 2 inch", "Mirrors", "50.8 mm diameter.",
            "Protected aluminium, laboratory", io::DiskDoc{0.0254, 0.0});
        add("Flat mirror, square 50 mm", "Mirrors", "50 x 50 mm.",
            "Back-silvered glass mirror", io::PlaneDoc{0.025, 0.025});
        add("Concave mirror, f = 100 mm", "Mirrors",
            "A parabola, focus at +Z. On-axis only: the engine has no off-axis parabola, which "
            "is the usual lab way to focus without blocking the beam.",
            "Protected aluminium, laboratory", io::ParaboloidDoc{0.1, 0.0254});
        add("Solar dish, 1 m", "Mirrors", "f/D = 0.5, the classic cooker dish.",
            "Kitchen aluminium foil", io::ParaboloidDoc{0.5, 0.5});
        add("Solar trough, 2 m", "Mirrors", "Focuses to a line rather than a point.",
            "Kitchen aluminium foil", io::CylParaboloidDoc{0.3, 0.4, 1.0});
        add("Flat panel reflector", "Mirrors", "The panel-cooker reflector.",
            "Kitchen aluminium foil", io::PlaneDoc{0.35, 0.25});

        // ---- Lenses. f = R / (n - 1) for a plano-convex in N-BK7, n = 1.5168.
        add("Plano-convex lens, f = 50 mm", "Lenses",
            "1 inch, N-BK7, curved face toward +Z. Put the curved face toward the collimated "
            "side to keep spherical aberration down.",
            "N-BK7 borosilicate crown", io::ThickLensDoc{0.0258, 0.0, 0.0053, 0.0254});
        add("Plano-convex lens, f = 100 mm", "Lenses", "1 inch, N-BK7.",
            "N-BK7 borosilicate crown", io::ThickLensDoc{0.0517, 0.0, 0.0036, 0.0254});
        add("Plano-convex lens, f = 200 mm", "Lenses", "1 inch, N-BK7.",
            "N-BK7 borosilicate crown", io::ThickLensDoc{0.1034, 0.0, 0.0028, 0.0254});
        add("Bi-convex lens, f = 50 mm", "Lenses",
            "1 inch, N-BK7, symmetric. Better than a plano-convex at 1:1 conjugates.",
            "N-BK7 borosilicate crown", io::ThickLensDoc{0.0517, -0.0517, 0.0060, 0.0254});
        add("Bi-concave lens, f = -50 mm", "Lenses",
            "1 inch, N-BK7. Negative focal length: it diverges a beam.",
            "N-BK7 borosilicate crown", io::ThickLensDoc{-0.0517, 0.0517, 0.0025, 0.0254});
        add("Fresnel lens, f = 300 mm", "Lenses",
            "A FLAT lens with per-zone normals, not a lens body: one refracting event at zero "
            "thickness. The solar-cooker lens.",
            "PMMA (acrylic)", io::FresnelZoneLensDoc{0.3, 0.01, 0.001, 60, 1.4917});

        // ---- Splitters.
        add("Beamsplitter plate, 1 inch", "Splitters",
            "Usually used at 45 degrees. Zero thickness, so no ghost from a second face.",
            "Beamsplitter 50:50", io::DiskDoc{0.0127, 0.0});
        add("Pellicle beamsplitter, 1 inch", "Splitters",
            "The splitter a zero-thickness surface models correctly.",
            "Pellicle beamsplitter 45:55", io::DiskDoc{0.0127, 0.0});

        // ---- Apertures (gap G3, new in Wave 3).
        add("Iris, 2 mm aperture", "Apertures", "25 mm stop with a 2 mm hole.",
            "Black anodised aluminium", io::DiskDoc{0.0125, 0.001});
        add("Iris, 5 mm aperture", "Apertures", "25 mm stop with a 5 mm hole.",
            "Black anodised aluminium", io::DiskDoc{0.0125, 0.0025});
        add("Single slit, 100 microns", "Apertures",
            "The width that makes visible-light diffraction obvious. Note the ray tracer draws "
            "a sharp-edged beam through it; the diffraction pattern needs the wave engine, "
            "which is Wave 6.",
            "Black anodised aluminium", io::SlitPlateDoc{0.02, 0.02, 0.0001, 1, 0.0});
        add("Double slit, 100 microns / 0.5 mm apart", "Apertures", "As above.",
            "Black anodised aluminium", io::SlitPlateDoc{0.02, 0.02, 0.0001, 2, 0.0005});
        add("Beam dump", "Apertures", "A dark matte disk to end a beam on.",
            "Beam dump", io::DiskDoc{0.02, 0.0});

        // ---- Detectors and screens.
        add("White screen card, 50 mm", "Detectors",
            "A matte white card: scatters the beam so you can see where it lands. It does NOT "
            "measure anything - the receiver is what records flux.",
            "White card screen", io::DiskDoc{0.025, 0.0});
        add("Power meter head, 10 mm", "Detectors",
            "An absorbing disk standing in for a meter head. As above, it does not record: make "
            "it the receiver if you want a number.",
            "Ideal black (reference)", io::DiskDoc{0.005, 0.0});
        return c;
    }();
    return v;
}

const std::vector<SourceEntry>& builtin_sources() {
    static const std::vector<SourceEntry> v = [] {
        std::vector<SourceEntry> s;
        auto laser = [](const char* name, const char* note, double nm, double watts,
                        double dia_m, double div_mrad) {
            io::LaserSourceDoc l;
            l.origin          = {0.0, 0.0, 0.3};
            l.direction       = {0.0, 0.0, -1.0};
            l.power_w         = watts;
            l.wavelength_nm   = nm;
            l.beam_diameter_m = dia_m;
            l.divergence_mrad = div_mrad;
            SourceEntry e;
            e.name = name; e.note = note; e.doc = l;
            return e;
        };
        s.push_back(laser("Helium-neon laser, 633 nm",
                          "Catalogue wavelength; the rest typical. The classic red bench laser, "
                          "and the right default for interferometry.", 632.8, 0.005, 0.0008, 1.3));
        s.push_back(laser("Green DPSS laser, 532 nm",
                          "Catalogue wavelength. Brightest to the eye, so it photographs best.",
                          532.0, 0.005, 0.0015, 1.5));
        s.push_back(laser("Red diode laser, 650 nm", "Typical pointer module.",
                          650.0, 0.005, 0.003, 2.0));
        s.push_back(laser("Violet diode laser, 405 nm",
                          "Shows dispersion most strongly against the 633 nm line.",
                          405.0, 0.02, 0.002, 2.0));
        s.push_back(laser("Infrared diode, 1064 nm",
                          "Pair it with the gold mirror, whose figure is only honest here.",
                          1064.0, 0.05, 0.002, 2.0));

        io::SunSourceDoc sun;
        sun.direction     = math::vec3{0.0, 0.0, -1.0};
        sun.dni_wm2       = 1000.0;
        sun.aperture.mode = "auto_fit";
        SourceEntry e;
        e.name = "Sun, overhead, clear day";
        e.note = "1000 W/m2 direct normal irradiance with a 4.65 mrad pillbox disk. Its "
                 "collection aperture is fitted to whatever is in the scene when you add it.";
        e.doc  = sun;
        s.push_back(std::move(e));
        return s;
    }();
    return v;
}

std::filesystem::path user_materials_path() {
    return config_path().parent_path() / "materials.json";
}

std::vector<MaterialEntry> load_user_materials() {
    std::vector<MaterialEntry> out;
    try {
        std::ifstream in(user_materials_path());
        if (!in) return out;
        json j;
        in >> j;
        if (!j.is_array()) return out;
        for (const auto& e : j) {
            MaterialEntry m;
            m.name  = e.value("name", std::string());
            m.group = "My materials";
            m.note  = e.value("note", std::string("Your own entry."));
            m.doc.id   = m.name;
            m.doc.type = e.value("type", std::string());
            m.doc.params = e.value("params", json::object());
            m.user_defined = true;
            if (!m.name.empty() && !m.doc.type.empty())
                out.push_back(std::move(m));
        }
    } catch (...) {
        // A corrupt library must not stop the app starting; the built-ins still work.
    }
    return out;
}

bool save_user_materials(const std::vector<MaterialEntry>& entries) {
    try {
        const auto path = user_materials_path();
        std::filesystem::create_directories(path.parent_path());
        json j = json::array();
        for (const auto& e : entries) {
            if (!e.user_defined) continue;
            j.push_back(json{{"name", e.name}, {"note", e.note},
                             {"type", e.doc.type}, {"params", e.doc.params}});
        }
        std::ofstream out(path);
        if (!out) return false;
        out << j.dump(2) << '\n';
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

} // namespace scrt::viz
