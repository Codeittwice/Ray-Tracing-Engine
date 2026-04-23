#include "scrt/optics/Paraxial.hpp"

namespace scrt::optics {

// Column-major GLM layout: M[col][row].
// ABCD matrix [[A,B],[C,D]] → col0=(A,C), col1=(B,D).

Mat2 abcd_free_space(double d) {
    return Mat2{1.0, 0.0,   // col 0: (A,C) = (1,0)
                d,   1.0};  // col 1: (B,D) = (d,1)
}

Mat2 abcd_thin_lens(double f) {
    // [[1, 0], [-1/f, 1]]: col0=(A,C)=(1,-1/f), col1=(B,D)=(0,1)
    return Mat2{1.0, -1.0 / f,   // col 0
                0.0,  1.0};      // col 1
}

Mat2 abcd_flat_refraction(double n1, double n2) {
    // Paraxial Snell: n1*theta = n2*theta' → theta' = (n1/n2)*theta
    return Mat2{1.0,       0.0,          // col 0
                0.0, n1 / n2};           // col 1
}

Mat2 abcd_spherical_refraction(double n1, double n2, double R) {
    // [[1, 0], [(n1-n2)/(n2*R), n1/n2]]: col0=(A,C)=(1,(n1-n2)/(n2*R)), col1=(B,D)=(0,n1/n2)
    return Mat2{1.0,                  (n1 - n2) / (n2 * R),   // col 0
                0.0,                  n1 / n2};                // col 1
}

Mat2 abcd_spherical_mirror(double R) {
    // [[1, 0], [-2/R, 1]]: col0=(A,C)=(1,-2/R), col1=(B,D)=(0,1)
    return Mat2{1.0,      -2.0 / R,   // col 0
                0.0,       1.0};      // col 1
}

Mat2 cascade(std::span<const Mat2> elements_in_order) {
    Mat2 M{1.0};  // identity
    for (const auto& e : elements_in_order)
        M = e * M;
    return M;
}

ParaxialRay apply(const Mat2& M, ParaxialRay in) {
    math::vec2 v = M * math::vec2{in.y, in.theta};
    return {v.x, v.y};
}

} // namespace scrt::optics
