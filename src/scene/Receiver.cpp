#include "scrt/scene/Receiver.hpp"

namespace scrt::scene {

Receiver::Receiver(double half_width, double half_height, int nx, int ny)
    : plane_(half_width, half_height), acc_(half_width, half_height, nx, ny) {}

void Receiver::set_transform(core::Transform t) {
    plane_.set_transform(std::move(t));
}

} // namespace scrt::scene
