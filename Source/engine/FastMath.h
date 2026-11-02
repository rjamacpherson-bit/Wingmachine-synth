#pragma once
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846 // MSVC doesn't define it by default
#endif
namespace pm {
// sin(2*pi*x) for any phase x: quadrant fold + 13th-order odd polynomial, error < 1e-7 (-140 dB), ~4x cheaper than std::sin
inline double sin2pi (double x) {
    double u = x - std::floor (x); if (u > .5) u -= 1;
    if (u > .25) u = .5 - u; else if (u < -.25) u = -.5 - u;
    const double z = 2 * M_PI * u, z2 = z * z;
    return z * (1 + z2 * (-1. / 6 + z2 * (1. / 120 + z2 * (-1. / 5040 + z2 * (1. / 362880 + z2 * (-1. / 39916800 + z2 * (1. / 6227020800.)))))));
}
}
