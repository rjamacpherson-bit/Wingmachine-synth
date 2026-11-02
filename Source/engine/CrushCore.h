// AKME D+0 MUSIC · Plunger Model — bit crusher (C++ port of CrushCore)
#pragma once
#include <cmath>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846 // MSVC doesn't define it by default
#endif
namespace pm {
class CrushCore {
public:
    struct Params { double bits = 16, rate = 48000, drive = 0, tone = 16000, mix = 0; } p;
    explicit CrushCore (double sampleRate) : sr (sampleRate) {}
    void process (const float* iL, const float* iR, float* oL, float* oR, int n) {
        const double step = std::min (1., p.rate / sr), q = std::pow (2., std::max (0., p.bits - 1)), g = 1 + p.drive * 8, ng = g > 1 ? std::tanh (g) : 1;
        const double tc = 1 - std::exp (-2 * M_PI * std::min (p.tone, sr * .45) / sr);
        for (int i = 0; i < n; ++i) {
            mS += (p.mix - mS) * .002; const double xL = iL[i], xR = iR[i];
            if (mS < 1e-5) { oL[i] = (float) xL; oR[i] = (float) xR; continue; }
            ph += step; if (ph >= 1) { ph -= 1; hL = std::round (std::tanh (xL * g) / ng * q) / q; hR = std::round (std::tanh (xR * g) / ng * q) / q; }
            lL += tc * (hL - lL); lR += tc * (hR - lR);
            oL[i] = (float) (xL * (1 - mS) + lL * mS); oR[i] = (float) (xR * (1 - mS) + lR * mS);
        }
    }
private:
    double sr, ph = 1, hL = 0, hR = 0, lL = 0, lR = 0, mS = 0;
};
}
