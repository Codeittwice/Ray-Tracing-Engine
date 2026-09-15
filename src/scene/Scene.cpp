#include "scrt/scene/Scene.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace scrt::scene {

std::size_t Scene::add_material(std::unique_ptr<materials::Material> m) {
    materials_.push_back(std::move(m));
    return materials_.size() - 1;
}

std::uint64_t Scene::add_surface(std::unique_ptr<surfaces::Surface> s) {
    const std::uint64_t id = next_surface_id_++;
    s->set_id(id);
    surfaces_.push_back(std::move(s));
    // A surface added after build_acceleration_structure() is otherwise invisible to a
    // stale BVH, so any add — not just remove — must force the linear-scan fallback.
    mark_acceleration_dirty();
    return id;
}

bool Scene::remove_surface(std::uint64_t id) {
    auto it = std::find_if(surfaces_.begin(), surfaces_.end(),
                           [id](const std::unique_ptr<surfaces::Surface>& s) {
                               return s->id() == id;
                           });
    if (it == surfaces_.end())
        return false;
    surfaces_.erase(it);
    // The BVH holds raw Surface* into the erased element; it must not be traversed again
    // until rebuilt.
    mark_acceleration_dirty();
    return true;
}

surfaces::Surface* Scene::surface_by_id(std::uint64_t id) {
    for (auto& s : surfaces_)
        if (s->id() == id)
            return s.get();
    return nullptr;
}

const surfaces::Surface* Scene::surface_by_id(std::uint64_t id) const {
    for (const auto& s : surfaces_)
        if (s->id() == id)
            return s.get();
    return nullptr;
}

std::optional<std::size_t> Scene::index_of(std::uint64_t id) const {
    for (std::size_t i = 0; i < surfaces_.size(); ++i)
        if (surfaces_[i]->id() == id)
            return i;
    return std::nullopt;
}

void Scene::set_receiver(std::unique_ptr<Receiver> r) { receiver_ = std::move(r); }
std::size_t Scene::add_source(std::unique_ptr<sources::LightSource> s) {
    if (!s)
        throw std::invalid_argument("Scene::add_source: null source");
    sources_.push_back(std::move(s));
    return sources_.size() - 1;
}

bool Scene::remove_source(std::size_t index) {
    if (index >= sources_.size())
        return false;
    sources_.erase(sources_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool Scene::remove_material(const std::string& name) {
    auto it = std::find_if(materials_.begin(), materials_.end(),
                           [&](const std::unique_ptr<materials::Material>& m) {
                               return m && m->name() == name;
                           });
    if (it == materials_.end())
        return false;
    materials_.erase(it);
    return true;
}

const sources::SunSource* Scene::primary_sun() const {
    for (const auto& s : sources_)
        if (const auto* sun = s->as_sun())
            return sun;
    return nullptr;
}

sources::SunSource* Scene::primary_sun() {
    for (auto& s : sources_)
        if (auto* sun = s->as_sun())
            return sun;
    return nullptr;
}

void Scene::build_acceleration_structure() {
    bvh_.build(surfaces_);
    use_bvh_     = !bvh_.empty();
    accel_dirty_ = false;
}

std::span<const std::unique_ptr<surfaces::Surface>> Scene::surfaces() const {
    return surfaces_;
}

std::span<std::unique_ptr<materials::Material>> Scene::mutable_materials() {
    return materials_;
}

std::span<std::unique_ptr<surfaces::Surface>> Scene::mutable_surfaces() {
    return surfaces_;
}

core::AABB Scene::world_bounds() const {
    core::AABB box;  // Inside-out until something expands it.
    bool any = false;

    for (const auto& s : surfaces_) {
        box.expand(s->world_bounds());
        any = true;
    }

    // Receiver faces are intersected directly in intersect(), not through the BVH, so
    // walking surfaces_ alone would under-size the bounds for box-cooker scenes.
    if (receiver_) {
        for (const auto& face : receiver_->faces()) {
            box.expand(face->surface()->world_bounds());
            any = true;
        }
    }

    if (!any)
        return core::AABB(math::vec3{0.0}, math::vec3{0.0});
    return box;
}

bool Scene::intersect(const core::Ray& r, double t_min, double t_max,
                      core::Hit& hit) const {
    bool   found  = false;
    double t_best = t_max;
    core::Hit tmp;

    if (use_bvh_ && !accel_dirty_) {
        if (bvh_.intersect(r, t_min, t_best, tmp)) {
            t_best = tmp.t;
            hit    = tmp;
            found  = true;
        }
    } else {
        for (const auto& s : surfaces_) {
            if (s->intersect(r, t_min, t_best, tmp)) {
                t_best = tmp.t;
                hit    = tmp;
                found  = true;
            }
        }
    }

    if (receiver_) {
        for (const auto& face : receiver_->faces()) {
            if (face->surface()->intersect(r, t_min, t_best, tmp)) {
                t_best = tmp.t;
                hit    = tmp;
                found  = true;
            }
        }
    }

    return found;
}

} // namespace scrt::scene
