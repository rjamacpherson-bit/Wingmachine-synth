// AKME D+0 MUSIC · Plunger Model — Stretch: grab the last stretch of the beat and play it back slower or faster
// Every Length (on the grid) it takes what just played and replays it at Speed, looping inside the window until the next grab (or for ever with Hold).
// Modes: Tracker (8-bit, nearest sample, pitch follows speed, like an old home-computer sampler), Sampler 12-bit (grainy time-stretch,
// the classic drum-machine warble), Modern (smooth overlapping grains, pitch kept), Smear (huge sprayed grains, extreme blur), Tape (varispeed).
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846 // MSVC doesn't define it by default
#endif
#include "Rng.h"

namespace pm {

class Stretch
{
public:
    struct Params { int mode = 2; double lenSec = 2, speed = .5, pitch = 0, grainMs = 80, jitter = 0, mix = 1, crush = .5; bool hold = false; };
    Params p;
    static constexpr int VIEW = 128; float view[VIEW] {}; float playFrac = 0; // for the display: the grabbed window and where playback is

    void init (double sampleRate) { sr = sampleRate; N = (int) (sr * 8.5); bL.assign ((size_t) N, 0.f); bR.assign ((size_t) N, 0.f); w = 0; segStart = 0; segLen = 0; pos = 0; for (auto& g : gr) g = {}; }
    void reset() { std::fill (bL.begin(), bL.end(), 0.f); std::fill (bR.begin(), bR.end(), 0.f); segLen = 0; pos = 0; mixS = 0; }
    bool idle() const { return p.mix <= 0 && mixS < 1e-5; }

    // boundary: the host grid says a new window starts here (sample offset within this block)
    void grab (int offset) {
        if (p.hold && segLen > 0) return;
        segLen = std::min ((int) (p.lenSec * sr), N - 16); segStart = (w + offset - segLen + N) % N; pos = 0;
        for (int i = 0; i < VIEW; ++i) { float m = 0; const int a = i * segLen / VIEW, b = (i + 1) * segLen / VIEW; for (int k = a; k < b; k += 8) m = std::max (m, std::abs (bL[(size_t) ((segStart + k) % N)])); view[i] = m; } }

    void process (double* L, double* R, int n, int grabAt = -1) {
        const double ratio = std::pow (2., p.pitch / 12), gLen = std::max (64., p.grainMs * .001 * sr * (p.mode == 3 ? 6 : 1));
        for (int i = 0; i < n; ++i) {
            bL[(size_t) w] = (float) L[i]; bR[(size_t) w] = (float) R[i];
            if (i == grabAt) grab (0);
            w = (w + 1) % N;
            mixS += (p.mix - mixS) * .002;
            if (segLen <= 0) continue;
            double yl = 0, yr = 0;
            if (p.mode == 0 || p.mode == 4) { // tracker / tape: one read head, speed sets the pitch too
                const double sp = p.speed * (p.mode == 0 ? ratio : 1); pos += sp; if (pos >= segLen) pos -= segLen;
                if (p.mode == 0) { // nearest sample, sample-and-hold at a low rate, 8-bit steps
                    if (--hold <= 0) { hold = std::max (1, (int) (sr / (8000 + 20000 * (1 - p.crush)))); const int k = (segStart + (int) pos) % N; hl = q (bL[(size_t) k], 128); hr = q (bR[(size_t) k], 128); } yl = hl; yr = hr; }
                else { const double fp = segStart + pos; yl = rd (bL, fp); yr = rd (bR, fp); }
            } else { // granular: the source position walks at Speed, each grain plays at Pitch
                pos += p.speed; if (pos >= segLen) pos -= segLen;
                const int G = p.mode == 1 ? 2 : 4; const double hop = gLen / G;
                if (++spawn >= hop) { spawn = 0; auto& g = gr[(size_t) (gi = (gi + 1) % G)]; g.on = true; g.t = 0; g.len = gLen;
                    const double jit = (p.jitter + (p.mode == 3 ? .6 : 0)) * gLen * (rng() * 2 - 1); g.src = std::fmod (pos + jit + segLen * 4, (double) segLen); g.rate = ratio * (p.mode == 1 ? 1 + .004 * (rng() - .5) : 1); }
                for (int k = 0; k < G; ++k) { auto& g = gr[(size_t) k]; if (! g.on) continue; const double u = g.t / g.len; if (u >= 1) { g.on = false; continue; }
                    const double win = p.mode == 1 ? std::min (1., std::min (u, 1 - u) * 8) : .5 - .5 * std::cos (2 * M_PI * u); // the sampler mode: square-ish windows = its gritty warble
                    const double fp = segStart + std::fmod (g.src + g.t * g.rate, (double) segLen); yl += rd (bL, fp) * win; yr += rd (bR, fp) * win; g.t += 1; }
                const double norm = p.mode == 1 ? .8 : 2. / G; yl *= norm; yr *= norm;
                if (p.mode == 1) { if (--hold <= 0) { hold = std::max (1, (int) (sr / 26040)); hl = q ((float) yl, 2048); hr = q ((float) yr, 2048); } yl = hl; yr = hr; } // 12-bit at 26 kHz
            }
            L[i] = L[i] * (1 - mixS) + yl * mixS; R[i] = R[i] * (1 - mixS) + yr * mixS;
        }
        playFrac = segLen > 0 ? (float) (pos / segLen) : 0.f;
    }
public:
    struct Grain { bool on = false; double t = 0, len = 1, src = 0, rate = 1; };
    double sr = 48000; int N = 1, w = 0, segStart = 0, segLen = 0, hold = 0, gi = 0; double pos = 0, spawn = 0, mixS = 0, hl = 0, hr = 0; std::vector<float> bL, bR; Grain gr[4]; Rng rng;
    static double q (float x, int steps) { return std::round (x * steps) / steps; }
    double rd (const std::vector<float>& b, double fp) const { fp = std::fmod (fp, (double) N); if (fp < 0) fp += N; const int i0 = (int) fp; const double f = fp - i0; return b[(size_t) i0] * (1 - f) + b[(size_t) ((i0 + 1) % N)] * f; }
};

} // namespace pm
