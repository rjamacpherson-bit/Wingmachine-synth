// AKME D+0 MUSIC · Plunger Model — XSpace: a delay and a reverb wired into each other
// Two modulated delay lines (left / right, their own times) and an 8-line feedback delay network reverb, with four cross-feeds:
// delay into reverb, reverb back into the delays, left <-> right, and the reverb's tail re-delayed. Ducking, damping, tilt and a freeze.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846 // MSVC doesn't define it by default
#endif
#include "FastMath.h"

namespace pm {

class XSpace
{
public:
    struct Params { double timeL = .375, timeR = .5, fb = .45, cross = .3, d2r = .5, r2d = .2, size = .6, decay = .7, damp = .4, mod = .3, duck = 0, mix = .3, tilt = 0; bool freeze = false; };
    Params p;

    void init (double sampleRate) {
        sr = sampleRate; const int dn = (int) (sr * 2.6) + 8; for (auto& b : dl) b.assign ((size_t) dn, 0.f); dN = dn;
        static const double LEN[8] = { .0297, .0371, .0411, .0437, .0533, .0613, .0719, .0827 };
        for (int k = 0; k < 8; ++k) { fdn[k].assign ((size_t) (sr * LEN[k] * 2.2) + 8, 0.f); fLen[k] = sr * LEN[k]; }
        static const double AP[4] = { .0047, .0067, .0089, .0113 }; for (int k = 0; k < 4; ++k) { ap[k].assign ((size_t) (sr * AP[k]) + 4, 0.f); apW[k] = 0; }
        reset(); }
    void reset() { for (auto& b : dl) std::fill (b.begin(), b.end(), 0.f); for (auto& b : fdn) std::fill (b.begin(), b.end(), 0.f); for (auto& b : ap) std::fill (b.begin(), b.end(), 0.f);
        dW = 0; for (int& w : fW) w = 0; for (double& z : lp) z = 0; tL = tR = 0; env = 0; mph = 0; dlpL = dlpR = 0; }
    bool idle() const { return p.mix <= 0 && mixS < 1e-5; }

    void process (double* L, double* R, int n) {
        const double tlS = std::min (p.timeL * sr, (double) dN - 8), trS = std::min (p.timeR * sr, (double) dN - 8);
        const double rt = .3 + 7.7 * p.decay * p.decay; // reverb time in seconds
        double fg[8]; for (int k = 0; k < 8; ++k) fg[k] = p.freeze ? .9995 : std::pow (10., -3 * (fLen[k] * (.5 + p.size)) / (rt * sr));
        const double dampA = 1 - (.05 + .9 * (1 - p.damp)), fbD = p.freeze ? 1.0 : std::min (1.05, p.fb), inG = p.freeze ? 0 : 1;
        const double ka = 1 - std::exp (-1 / (sr * .004)), kr = 1 - std::exp (-1 / (sr * .25));
        for (int i = 0; i < n; ++i) {
            const double xL = L[i], xR = R[i], xm = .5 * (xL + xR);
            env += ((std::abs (xm) > env) ? ka : kr) * (std::abs (xm) - env); const double duckG = 1 - p.duck * std::min (1., env * 4);
            mph += (.13 + .5 * p.mod) / sr; if (mph > 1) mph -= 1; const double md = p.mod * .004 * sr;
            // delays: read with a slow wobble, tilt the colour, cross-feed left <-> right and pull in some reverb
            const double rl = rd (dl[0], dW - tlS - md * (1 + sin2pi (mph))), rr = rd (dl[1], dW - trS - md * (1 + sin2pi (mph + .25)));
            dlpL += (rl - dlpL) * (.15 + .7 * (1 - p.damp)); dlpR += (rr - dlpR) * (.15 + .7 * (1 - p.damp));
            const double cl = dlpL + p.tilt * (rl - dlpL), cr = dlpR + p.tilt * (rr - dlpR);
            // reverb input: the dry sound plus the delays (delay -> reverb)
            double rin = xm * inG + (cl + cr) * .5 * p.d2r;
            for (int k = 0; k < 4; ++k) { const int sz = (int) ap[k].size(); const int rp = (apW[k] + 1) % sz; const double z = ap[k][(size_t) rp]; const double y = -.6 * rin + z; ap[k][(size_t) apW[k]] = (float) (rin + .6 * y); apW[k] = rp; rin = y; }
            // FDN: 8 lines, Householder mixing, damping per line
            double o[8], sum = 0; for (int k = 0; k < 8; ++k) { const double len = fLen[k] * (.5 + p.size) * (1 + .003 * p.mod * sin2pi (mph * (1 + k * .17))); o[k] = rd (fdn[k], fW[k] - len); sum += o[k]; }
            sum *= .25; double revL = 0, revR = 0;
            for (int k = 0; k < 8; ++k) { double v = (o[k] - sum) * fg[k]; lp[k] += (v - lp[k]) * (1 - dampA); v = lp[k] + (k & 1 ? rin * .35 : rin * .35);
                const int sz = (int) fdn[k].size(); fW[k] = (fW[k] + 1) % sz; fdn[k][(size_t) fW[k]] = (float) v; if (k & 1) revR += o[k]; else revL += o[k]; }
            revL *= .35; revR *= .35;
            // write the delays: input + their own feedback + the other side (cross) + the reverb (reverb -> delay)
            const double wl = xL * inG + fbD * ((1 - p.cross) * cl + p.cross * cr) + p.r2d * revL, wr = xR * inG + fbD * ((1 - p.cross) * cr + p.cross * cl) + p.r2d * revR;
            dW = (dW + 1) % dN; dl[0][(size_t) dW] = (float) std::tanh (wl * .9) / .9f; dl[1][(size_t) dW] = (float) std::tanh (wr * .9) / .9f;
            mixS += (p.mix - mixS) * .002; const double wet = mixS * duckG;
            L[i] = xL + (cl * .7 + revL) * wet; R[i] = xR + (cr * .7 + revR) * wet;
        }
    }
private:
    double sr = 48000; std::vector<float> dl[2], fdn[8], ap[4]; int dN = 1, dW = 0, fW[8] {}, apW[4] {}; double fLen[8] {}, lp[8] {}, tL = 0, tR = 0, env = 0, mph = 0, dlpL = 0, dlpR = 0, mixS = 0;
    static double rd (const std::vector<float>& b, double pos) { const int n = (int) b.size(); while (pos < 0) pos += n; const int i0 = (int) pos % n; const double f = pos - std::floor (pos); return b[(size_t) i0] * (1 - f) + b[(size_t) ((i0 + 1) % n)] * f; }
};

} // namespace pm
