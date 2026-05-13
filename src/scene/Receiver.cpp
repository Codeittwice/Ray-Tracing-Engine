#include "scrt/scene/Receiver.hpp"
#include <utility>

namespace scrt::scene {

ReceiverFace::ReceiverFace(std::string name, double half_width, double half_height,
                           int nx, int ny, ReceiverFaceMode mode)
    : name_(std::move(name)),
      mode_(mode),
      plane_(half_width, half_height),
      acc_(half_width, half_height, nx, ny) {
    plane_.set_name(name_);
}

void ReceiverFace::set_transform(core::Transform t) {
    plane_.set_transform(std::move(t));
}

void ReceiverFace::set_name(std::string name) {
    name_ = std::move(name);
    plane_.set_name(name_);
}

Receiver::Receiver(double half_width, double half_height, int nx, int ny)
{
    add_face("receiver", half_width, half_height, nx, ny, ReceiverFaceMode::RecordAbsorb);
}

void Receiver::set_transform(core::Transform t) {
    surface()->set_transform(std::move(t));
}

ReceiverFace& Receiver::add_face(std::string name, double half_width, double half_height,
                                 int nx, int ny, ReceiverFaceMode mode) {
    faces_.push_back(std::make_unique<ReceiverFace>(
        std::move(name), half_width, half_height, nx, ny, mode));
    return *faces_.back();
}

Receiver Receiver::clone_empty() const {
    Receiver clone(1.0, 1.0, 1, 1);
    clone.faces_.clear();
    for (const auto& face : faces_) {
        auto& new_face = clone.add_face(
            face->name(),
            face->accumulator().half_width(),
            face->accumulator().half_height(),
            face->accumulator().nx(),
            face->accumulator().ny(),
            face->mode());
        new_face.set_transform(face->surface()->transform());
    }
    return clone;
}

int Receiver::face_index_for_surface(const surfaces::Surface* surface) const {
    for (std::size_t i = 0; i < faces_.size(); ++i) {
        if (faces_[i]->surface() == surface)
            return static_cast<int>(i);
    }
    return -1;
}

void Receiver::clear_accumulators() noexcept {
    for (auto& face : faces_)
        face->accumulator().clear();
}

void Receiver::merge_from(const Receiver& other) noexcept {
    for (std::size_t i = 0; i < faces_.size() && i < other.faces_.size(); ++i)
        faces_[i]->accumulator().merge_from(other.faces_[i]->accumulator());
}

void Receiver::finalize(std::size_t total_primary_rays) {
    for (auto& face : faces_)
        face->accumulator().finalize(total_primary_rays);
}

} // namespace scrt::scene
