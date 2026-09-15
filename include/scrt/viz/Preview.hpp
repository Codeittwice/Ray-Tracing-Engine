#pragma once
#include "scrt/io/SceneDocument.hpp"
#include <filesystem>

namespace scrt::materials { class Material; }

namespace scrt::viz {

/// Draws a small 3D thumbnail of a surface as an ImGui item, `size` pixels square.
///
/// A real render of the real geometry: the surface is built with io::build_surface, tessellated
/// with the same Surface::tessellate the 3D view uses, projected orthographically from a fixed
/// three-quarter view, flat-shaded and painted into the ImGui draw list back to front. There are
/// no per-type icons and no image assets, so a thumbnail cannot drift from what the tracer
/// traces, and a surface type added later draws without touching this code.
///
/// NOTHING here is registered with Polyscope. A thumbnail mesh folded into the global
/// lengthScale and boundingBox would resize the ground plane and every relative length in the
/// scene; these are draw-list triangles and nothing else.
///
/// Results are cached per `key`, because a library entry's geometry never changes in a session.
/// A surface that cannot be built, or that tessellates to nothing (ImplicitSDF's tessellate is
/// an empty stub), draws a visible placeholder rather than an empty box.
void draw_surface_thumbnail(const char* key, const io::SurfaceDoc& sd,
                            const std::filesystem::path& base_dir, float size);

/// Draws a diagram of what a material DOES, as an ImGui item `w` x `h` pixels.
///
/// The outgoing rays are obtained by calling the material's own interact() with a fixed incoming
/// ray, a fixed surface normal and a fixed-seed Rng, and each is drawn with an opacity and
/// thickness set by the power it carries. So a 50:50 splitter and a 90:10 pickoff look
/// different, a thin pane visibly fails to bend its transmitted ray, and a diffuser draws its
/// own cosine fan. A material whose behaviour does not match its name is visible here.
void draw_material_diagram(const materials::Material& m, float w, float h);

/// Same diagram, from a document entry: the material is built once and cached under `key`.
/// A MaterialDoc that cannot be built draws the same placeholder a bad surface does, rather
/// than an empty box that reads as a working entry.
void draw_material_diagram(const char* key, const io::MaterialDoc& md, float w, float h);

} // namespace scrt::viz
