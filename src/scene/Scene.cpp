#include "scrt/scene/Scene.hpp"
#include <limits>

namespace scrt::scene {

std::size_t Scene::add_material(std::unique_ptr<materials::Material> m) {
    materials_.push_back(std::move(m));
    return materials_.size() - 1;
}

std::size_t Scene::add_surface(std::unique_ptr<surfaces::Surface> s) {
    surfaces_.push_back(std::move(s));
    return surfaces_.size() - 1;
}

void Scene::set_receiver(std::unique_ptr<Receiver> r) { receiver_ = std::move(r); }
void Scene::set_sun(std::unique_ptr<sources::SunSource> s) { sun_ = std::move(s); }
void Scene::set_aperture(Aperture a) { aperture_ = a; }

void Scene::build_acceleration_structure() {
    bvh_.build(surfaces_);
    use_bvh_ = !bvh_.empty();
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

bool Scene::intersect(const core::Ray& r, double t_min, double t_max,
                      core::Hit& hit) const {
    bool   found  = false;
    double t_best = t_max;
    core::Hit tmp;

    if (use_bvh_) {
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
