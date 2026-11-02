// AKME D+0 MUSIC · Plunger Model — algorithmic reverb (VST only)
// 8-line feedback delay network with input diffusion, modulated lines, per-band decay (low cut / high damping in the loop),
// early reflections, stereo width, shimmer (octave-up pitch shifter in the loop), freeze and ducking.
// Algorithms: 1 Room, 2 Hall, 3 Plate, 4 Chamber, 5 Cloud, 6 Shimmer, 7 Bloom, 8 Dark space   (0 = Classic, the convolution reverb, handled by the engine)
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace pm {

class ReverbCore {
public:
    struct Params { int algo = 2; double decay = 2.4, damp = .5, size = .6, diff = .7, mod = .3, loCut = 80, hiCut = 9000, width = 1, early = .3, shimmer = 0, duck = 0, shimSemis = 12, shimMod = 0, shimRate = .7; bool freeze = false; } p;

    explicit ReverbCore (double sampleRate) : sr (sampleRate) {
        const int maxLen = (int) (sr * .32) + 64;
        for (auto& d : line) { d.assign ((size_t) maxLen, 0.f); }
        for (int j = 0; j < 4; ++j) { apIn[j].assign ((size_t) (sr * .03) + 8, 0.f); }
        erL.assign ((size_t) (sr * .12) + 8, 0.f); erR.assign ((size_t) (sr * .12) + 8, 0.f);
        psBuf.assign (PSN, 0.f);
        for (int j = 0; j < 8; ++j) lfoPh[j] = j / 8.0;
    }
    void clear() {
        for (auto& d : line) std::fill (d.begin(), d.end(), 0.f); for (auto& a : apIn) std::fill (a.begin(), a.end(), 0.f);
        std::fill (erL.begin(), erL.end(), 0.f); std::fill (erR.begin(), erR.end(), 0.f); std::fill (psBuf.begin(), psBuf.end(), 0.f);
        for (int j = 0; j < 8; ++j) { lp[j] = hp[j] = 0; } env = 0; bloomG = 0; for (auto& z : spZ) z = 0; gateCnt = 0; gateG = 0; swCnt = 1e9;
    }
    // call when params change (cheap; done once per engine block)
    void update() {
        // algorithm character: base size scale, diffusion, modulation and density defaults are blended with the user's controls
        static const double SZ[12] = { 1, .35, 1, .7, .55, 1.25, 1.1, 1, 1.4, .45, .8, 1 }, MODX[12] = { 1, .5, 1, .6, .7, 1.6, 1.2, 1, 1.4, .3, .6, .8 }, ERX[12] = { 1, 1.4, .8, .3, 1.1, .4, .5, .3, .6, 0, .6, 0 };
        const int a = std::max (0, std::min (11, p.algo)); spring = a == 9; gated = a == 10; revSwell = a == 11; gateLen = std::max (.05, p.decay * .25) * sr; swellLen = std::max (.1, p.decay * .5) * sr;
        const double sc = (.25 + .9 * p.size) * SZ[a];
        static const double BASE[8] = { .0297, .0371, .0411, .0437, .0533, .0617, .0683, .0797 }; // mutually prime-ish lengths (s)
        for (int j = 0; j < 8; ++j) {
            len[j] = std::min ((double) line[0].size() - 40, std::max (40.0, BASE[j] * sc * sr * (a == 3 ? .8 : 1)));
            // per-line feedback gain for the requested RT60 (60 dB down after 'decay' seconds)
            const double rt = p.freeze ? 1e9 : std::max (.1, p.decay * (a == 8 ? 1.6 : 1));
            g[j] = std::pow (10., -3 * (len[j] / sr) / rt);
        }
        const double hc = std::min (sr * .45, p.hiCut * (1.15 - .9 * p.damp) * (a == 8 ? .45 : a == 3 ? 1.3 : 1));
        // short lines pass through the loop filters more often per second: open them up so small rooms still honour Decay
        const double passes = std::max (1., .9 / std::max (.15, sc));
        lpA = 1 - std::exp (-2 * M_PI * std::min (sr * .45, hc * passes) / sr); hpA = std::exp (-2 * M_PI * std::max (10., p.loCut / passes) / sr);
        modDepth = p.mod * MODX[a] * .0018 * sr; modRate = (.15 + .6 * p.mod) * (a == 5 ? .6 : 1);
        for (int j = 0; j < 8; ++j) { const double w = 2 * M_PI * modRate * (1 + j * .13) / sr; rc[j] = std::cos (w); rs[j] = std::sin (w); }
        if (++renorm > 64) { renorm = 0; for (int j = 0; j < 8; ++j) { const double mg = std::sqrt (lc[j] * lc[j] + ls[j] * ls[j]); if (mg > 1e-9) { lc[j] /= mg; ls[j] /= mg; } } } // keep the phasors on the unit circle
        apG = .35 + .4 * p.diff * (a == 3 ? 1.15 : 1); apLen[0] = .0047 * sr; apLen[1] = .0071 * sr; apLen[2] = .0113 * sr; apLen[3] = .0167 * sr;
        for (double& l : apLen) l = std::min ((double) apIn[0].size() - 4, l * (a == 5 ? 1.6 : 1));
        erAmt = p.early * ERX[a]; shim = p.shimmer + (a == 6 ? .45 : 0); shimBase = std::pow (2., p.shimSemis / 12); shimInc = 2 * M_PI * p.shimRate / sr; bloom = a == 7; cloud = a == 5;
        psWin = (int) (.08 * sr);
    }
    // one stereo sample in, one out (wet only)
    inline void process (double inL, double inR, double& oL, double& oR) {
        double x = (inL + inR) * .5;
        if (spring) { // dispersion: a long chain of tiny allpasses makes the 'boing' chirp of a spring tank
            for (int k = 0; k < 16; ++k) { const double y = -.62 * x + spZ[k]; spZ[k] = x + .62 * y; x = y; } }
        // ducking: the tail dips while you play, then blooms back
        const double ax = std::abs (x); env += (ax - env) * (ax > env ? .01 : .0004);
        const double duckG = 1 / (1 + p.duck * 12 * env);
        double in = p.freeze ? 0 : x;
        // bloom: input level fades in slowly so the tail swells after each note
        if (bloom) { bloomG += ((ax > 1e-3 ? 1 : 0) - bloomG) * .00004; in *= .3 + .7 * bloomG; }
        // early reflections: a short multi-tap pattern, panned
        erL[(size_t) ew] = (float) inL; erR[(size_t) ew] = (float) inR;
        double eL = 0, eR = 0;
        if (erAmt > 0) { static const double TL[6] = { .0043, .0109, .0187, .0271, .0353, .0461 }, TR[6] = { .0061, .0131, .0213, .0293, .0397, .0499 }, GE[6] = { .8, .65, .55, .45, .35, .28 };
            const double s2 = .4 + .9 * p.size; const int N = (int) erL.size();
            for (int k = 0; k < 6; ++k) { int iL = ew - (int) (TL[k] * s2 * sr); if (iL < 0) iL += N; int iR = ew - (int) (TR[k] * s2 * sr); if (iR < 0) iR += N;
                eL += erL[(size_t) iL] * GE[k] * ((k & 1) ? .5 : 1); eR += erR[(size_t) iR] * GE[k] * ((k & 1) ? 1 : .5); }
            eL *= erAmt * .6; eR *= erAmt * .6; }
        if (++ew >= (int) erL.size()) ew = 0;
        // input diffusion: four series allpasses smear the attack into a dense wash
        for (int k = 0; k < 4; ++k) { auto& b = apIn[(size_t) k]; const int N = (int) b.size(); int r = apW - (int) apLen[k]; if (r < 0) r += N;
            const double d = b[(size_t) r], v = in + apG * d; b[(size_t) apW] = (float) v; in = d - apG * v; }
        if (++apW >= (int) apIn[0].size()) apW = 0;
        // read the 8 modulated lines
        double y[8]; const int N = (int) line[0].size();
        for (int j = 0; j < 8; ++j) {
            // LFO as a rotating phasor: two multiplies instead of a sine per line per sample
            { const double c0 = lc[j], s0 = ls[j]; lc[j] = c0 * rc[j] - s0 * rs[j]; ls[j] = s0 * rc[j] + c0 * rs[j]; }
            const double m = cloud ? (ls[j] + ls[j] * lc[j]) * .67 : ls[j];
            double rp = w - len[j] - modDepth * (1 + m); while (rp < 0) rp += N;
            const int i0 = (int) rp, i1 = i0 + 1 >= N ? 0 : i0 + 1; const double fr = rp - i0;
            double s = line[(size_t) j][(size_t) i0] * (1 - fr) + line[(size_t) j][(size_t) i1] * fr;
            lp[j] += (s - lp[j]) * lpA; s = lp[j];                     // high damping
            hp[j] = hp[j] * hpA + s * (1 - hpA); s -= hp[j];            // low cut (the remaining highs pass)
            y[j] = s * g[j];
        }
        // shimmer: octave-up pitch shift of the tail, fed back in
        double sh = 0;
        if (shim > 0) { const double t = (y[0] + y[3] + y[5]) * .5; psBuf[(size_t) psW] = (float) t;
            // pitch: the read pointer runs at 'ratio' (2 = octave up); the wobble bends it by up to +-60 cents with a slightly drifting LFO
            shLfo += shimInc * (1 + .15 * std::sin (shLfo * .37)); if (shLfo > 6283.1853) shLfo -= 6283.1853;
            const double ratio = shimBase * (1 + .0005776 * 60 * p.shimMod * std::sin (shLfo));
            psPh += ratio - 1; if (psPh >= psWin) psPh -= psWin; if (psPh < 0) psPh += psWin;
            for (int k = 0; k < 2; ++k) { double ph = psPh + k * psWin * .5; if (ph >= psWin) ph -= psWin; const double dly = psWin - ph; double rp = psW - dly; if (rp < 0) rp += PSN;
                const int i0 = (int) rp; const double fr = rp - i0; const double v = psBuf[(size_t) i0] * (1 - fr) + psBuf[(size_t) ((i0 + 1) & (PSN - 1))] * fr;
                sh += v * std::sin (M_PI * ph / psWin); }
            psW = (psW + 1) & (PSN - 1); sh *= std::min (.9, shim) * .45; }
        // Hadamard 8x8 mixing (fast butterfly), then write back with the input spread over the lines
        double h[8]; for (int j = 0; j < 8; ++j) h[j] = y[j];
        for (int st = 1; st < 8; st <<= 1) for (int j = 0; j < 8; j += st * 2) for (int k = j; k < j + st; ++k) { const double a = h[k], b = h[k + st]; h[k] = a + b; h[k + st] = a - b; }
        const double nrm = .35355339059327373; // 1/sqrt(8)
        static const double SG[8] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        for (int j = 0; j < 8; ++j) { double v = h[j] * nrm + (in + sh) * SG[j] * .5; if (p.freeze) v = h[j] * nrm; if (v > 4) v = 4; else if (v < -4) v = -4; line[(size_t) j][(size_t) w] = (float) v; }
        if (++w >= N) w = 0;
        // outputs: different line sums left and right, width blends them
        const double l = (y[0] - y[2] + y[4] + y[6] - y[7]) * .45, r = (y[1] + y[3] - y[5] + y[7] - y[6]) * .45, mid = (l + r) * .5, side = (l - r) * .5 * p.width;
        oL = ((mid + side) + eL) * duckG; oR = ((mid - side) + eR) * duckG;
        if (spring) { const double s2 = std::tanh (oL * 1.5) / 1.3; oL = s2 + (oL - s2) * .3; oR = std::tanh (oR * 1.5) / 1.3; }
        if (gated) { // the tail is cut a fixed time after each hit (Decay sets the gate length)
            if (ax > gateThr) { gateCnt = gateLen; gateThr = std::max (.004, ax * .6); } else gateThr *= .9995;
            const double tgt = gateCnt > 0 ? 1 : 0; if (gateCnt > 0) gateCnt -= 1; gateG += (tgt - gateG) * (tgt > gateG ? .05 : .004); oL *= gateG * 1.6; oR *= gateG * 1.6; }
        if (revSwell) { // each new hit restarts a swell: the tail rises instead of decaying, then stops, like a reversed reverb
            if (ax > swThr && swCnt > swellLen * .5) swCnt = 0; swThr = std::max (.004, std::max (swThr * .9997, ax * .7));
            swCnt += 1; const double ph = swCnt / swellLen, gS = ph < 1 ? ph * ph : std::max (0., 1 - (ph - 1) * 40); oL *= gS * 1.8; oR *= gS * 1.8; }
    }
private:
    double sr; std::vector<float> line[8], apIn[4], erL, erR, psBuf; static constexpr int PSN = 1 << 14;
    double len[8] {}, g[8] {}, lp[8] {}, hp[8] {}, lfoPh[8] {}, apLen[4] {}; int w = 0, apW = 0, ew = 0, psW = 0, psWin = 4000; double psPh = 0;
    double lc[8] { 1, .7, 0, -.7, -1, -.7, 0, .7 }, ls[8] { 0, .7, 1, .7, 0, -.7, -1, -.7 }, rc[8] { 1, 1, 1, 1, 1, 1, 1, 1 }, rs[8] {}; int renorm = 0; double spZ[16] {}; bool spring = false, gated = false, revSwell = false; double gateLen = 1, swellLen = 1, gateCnt = 0, gateThr = .01, gateG = 0, swCnt = 1e9, swThr = .01;
    double shLfo = 0, shimInc = 0, shimBase = 2; // shimmer interval ratio and wobble LFO step
    double lpA = .5, hpA = .99, modDepth = 0, modRate = .3, apG = .6, erAmt = 0, shim = 0, env = 0, bloomG = 0; bool bloom = false, cloud = false;
};

} // namespace pm
