#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <cstdint>
#include <vector>

namespace scrt::surfaces {

/// Ten scalar coefficients of Ax²+By²+Cz²+Dxy+Exz+Fyz+Gx+Hy+Iz+J=0.
struct QuadricCoeffs {
    double A = 0, B = 0, C = 0;  ///< Quadratic: x², y², z².
    double D = 0, E = 0, F = 0;  ///< Cross: xy, xz, yz.
    double G = 0, H = 0, I = 0;  ///< Linear: x, y, z.
    double J = 0;                 ///< Constant.
};

/// General quadric surface clipped to a local-space axis-aligned aperture box.
class GeneralQuadric final : public Surface {
public:
    /// @param coeffs  Ten quadric coefficients defining the implicit surface.
    /// @param clip    Local-space AABB that clips which part of the quadric is active.
    GeneralQuadric(QuadricCoeffs coeffs, core::AABB clip);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB world_bounds() const override;
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

private:
    QuadricCoeffs coeffs_;
    core::AABB    clip_;

    double     eval(math::vec3 p) const noexcept;
    math::vec3 grad(math::vec3 p) const noexcept;
};

} // namespace scrt::surfaces
