#include "scrt/io/ResultsExporter.hpp"
#include "scrt/math/Vec.hpp"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace scrt::io {

// ---- CSV -------------------------------------------------------------------

void export_flux_csv(const tracer::FluxAccumulator& acc,
                     const std::filesystem::path& out) {
    std::ofstream f(out);
    if (!f)
        throw std::runtime_error("export_flux_csv: cannot open '" + out.string() + "'");

    const auto& flux = acc.flux_map_wm2();
    const int nx = acc.nx(), ny = acc.ny();
    f << std::fixed << std::setprecision(6);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            f << flux[static_cast<std::size_t>(j * nx + i)];
            if (i + 1 < nx)
                f << ',';
        }
        f << '\n';
    }
}

// ---- NPY -------------------------------------------------------------------

void export_flux_npy(const tracer::FluxAccumulator& acc,
                     const std::filesystem::path& out) {
    std::ofstream f(out, std::ios::binary);
    if (!f)
        throw std::runtime_error("export_flux_npy: cannot open '" + out.string() + "'");

    const int nx = acc.nx(), ny = acc.ny();

    // Build NumPy 1.0 header dict.
    std::ostringstream hdr;
    hdr << "{'descr': '<f8', 'fortran_order': False, 'shape': ("
        << ny << ", " << nx << "), }";
    std::string hdr_str = hdr.str();

    // Pad header to a multiple of 64 bytes (header_len field + 10 magic bytes).
    // Total preamble = 10 + 2 (header_len) + hdr_str.size() + padding
    // Must be multiple of 64: so hdr_str + padding ends on 64-byte boundary.
    const std::size_t preamble_fixed = 12; // 6 magic + 2 version + 2 hdr_len + 2 pad
    std::size_t hdr_len = hdr_str.size() + 1; // +1 for '\n' terminator
    // Pad so that preamble_fixed + hdr_len is a multiple of 64.
    while ((preamble_fixed + hdr_len) % 64 != 0)
        ++hdr_len;
    hdr_str.resize(hdr_len - 1, ' ');
    hdr_str += '\n';

    // Magic: "\x93NUMPY", version 1.0, little-endian header length uint16
    const char magic[] = "\x93NUMPY\x01\x00";
    f.write(magic, 8);
    auto hlen = static_cast<std::uint16_t>(hdr_str.size());
    f.write(reinterpret_cast<const char*>(&hlen), 2);
    f.write(hdr_str.data(), static_cast<std::streamsize>(hdr_str.size()));

    // Data: float64 row-major.
    f.write(reinterpret_cast<const char*>(acc.flux_map_wm2().data()),
            static_cast<std::streamsize>(acc.flux_map_wm2().size() * sizeof(double)));
}

// ---- JSON summary ----------------------------------------------------------

void export_summary_json(const tracer::FluxAccumulator& acc,
                         const tracer::TraceResult& result,
                         double dni_wm2,
                         const std::filesystem::path& out) {
    std::ofstream f(out);
    if (!f)
        throw std::runtime_error("export_summary_json: cannot open '" + out.string() + "'");

    nlohmann::json j;
    j["total_power_w"]          = acc.total_power_w();
    j["peak_flux_wm2"]          = acc.peak_flux_wm2();
    j["concentration_ratio"]    = acc.concentration_ratio(dni_wm2);
    j["primary_rays_traced"]    = result.primary_rays_traced;
    j["total_hits"]             = result.total_hits;
    j["wall_time_s"]            = result.wall_time_s;
    j["grid_nx"]                = acc.nx();
    j["grid_ny"]                = acc.ny();
    j["bin_width_m"]            = acc.bin_width_m();
    j["bin_height_m"]           = acc.bin_height_m();

    f << j.dump(4) << '\n';
}

// ---- OBJ -------------------------------------------------------------------

void export_scene_obj(const scene::Scene& scene, int nseg,
                      const std::filesystem::path& out) {
    std::ofstream f(out);
    if (!f)
        throw std::runtime_error("export_scene_obj: cannot open '" + out.string() + "'");

    f << "# Solar Cooker Ray Tracer scene export\n";

    std::uint32_t vtx_offset = 1; // OBJ indices are 1-based

    auto write_group = [&](const std::string& name,
                           const surfaces::Surface* surf) {
        std::vector<math::vec3>    verts;
        std::vector<std::uint32_t> indices;
        surf->tessellate(nseg, verts, indices);
        if (verts.empty())
            return;

        f << "o " << name << '\n';
        for (const auto& v : verts)
            f << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            f << "f " << (vtx_offset + indices[i])
              << ' '  << (vtx_offset + indices[i + 1])
              << ' '  << (vtx_offset + indices[i + 2]) << '\n';
        }
        vtx_offset += static_cast<std::uint32_t>(verts.size());
    };

    for (const auto& s : scene.surfaces())
        write_group(s->name().empty() ? "surface" : s->name(), s.get());

    if (scene.receiver())
        write_group("receiver", scene.receiver()->surface());
}

} // namespace scrt::io
