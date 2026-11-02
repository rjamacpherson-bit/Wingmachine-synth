// Wing Machine engine: shared helpers, the small DSP blocks that stood in for Web Audio nodes (biquad, compressor, pink noise),
// and the two new shelf algorithms ported from JavaScript (FlutterVoice, SwarmBank).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <string>

namespace wm {

constexpr double PI = 3.14159265358979323846, TAU = 2 * PI;
inline double clampd (double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double wrapPi (double a) { a = std::fmod (a, TAU); if (a > PI) a -= TAU; if (a < -PI) a += TAU; return a; }
inline double foldHz (double f, double lo, double hi) { while (f < lo) f *= 2; while (f > hi) f /= 2; return f; }

struct Rnd {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    double operator()() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return (double) ((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0); }
};
inline Rnd& rng() { static Rnd r; return r; }
inline double rnd() { return rng()(); }

// ---- biquad (RBJ). Q for lowpass/highpass is in dB, as in Web Audio. ----
struct Biquad {
    enum Type { LP, HP, BP };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set (Type t, double f, double q, double sr) {
        f = clampd (f, 10.0, sr * 0.49); const double w = TAU * f / sr, cw = std::cos (w), sw = std::sin (w);
        double qq = (t == BP) ? std::max (0.01, q) : std::pow (10.0, q / 20.0), al = sw / (2 * qq), a0 = 1 + al;
        if (t == LP) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }
        else if (t == HP) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; }
        else { b0 = al; b1 = 0; b2 = -al; }
        a1 = -2 * cw / a0; a2 = (1 - al) / a0; b0 /= a0; b1 /= a0; b2 /= a0;
    }
    inline double process (double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    void clear() { x1 = x2 = y1 = y2 = 0; }
};

// ---- pink noise (the same Paul Kellet filter as the web version), shared by every noise source ----
struct PinkBuf {
    std::vector<float> d;
    void make (int len, double scale, bool normalise) {
        d.assign ((size_t) len, 0.f); double b0 = 0, b1 = 0, b2 = 0, pk = 1e-4;
        for (int k = 0; k < len; ++k) { const double w = rnd() * 2 - 1; b0 = .99765 * b0 + w * .099046; b1 = .963 * b1 + w * .2965164; b2 = .57 * b2 + w * 1.0526913; const double v = b0 + b1 + b2 + w * .1848; d[(size_t) k] = (float) v; pk = std::max (pk, std::abs (v)); }
        for (auto& v : d) v = (float) (normalise ? v / pk : v * scale);
    }
};

// ---- a compressor shaped like the Web Audio DynamicsCompressor (static curve, soft knee, automatic make-up gain), without the lookahead ----
struct Comp {
    double thr = -22, knee = 8, ratio = 12, att = .002, rel = .14, sr = 48000;
    void prepare (double s) { sr = s; solve(); }
    void set (double t, double k, double r, double a, double rl) { thr = t; knee = k; ratio = r; att = a; rel = rl; solve(); }
    void process (double* L, double* R, int n) {
        const double ka = 1 - std::exp (-1 / (sr * att)), kr = 1 - std::exp (-1 / (sr * rel));
        for (int i = 0; i < n; ++i) {
            const double x = std::max (std::abs (L[i]), std::abs (R[i]));
            const double tg = x > 1e-9 ? sat (x) / x : 1;
            g += (tg - g) * (tg < g ? ka : kr);
            L[i] *= g * makeup; R[i] *= g * makeup;
        }
    }
private:
    double g = 1, makeup = 1, kk = 1, lin = 0, kneeLin = 0, yk = 0;
    static double db2l (double d) { return std::pow (10.0, d / 20.0); }
    static double l2db (double l) { return 20 * std::log10 (std::max (l, 1e-12)); }
    double kneeCurve (double x, double k) const { return x < lin ? x : lin + (1 - std::exp (-k * (x - lin))) / k; }
    double sat (double x) const {
        if (x < lin) return x;
        if (x < kneeLin) return kneeCurve (x, kk);
        return db2l (yk + (l2db (x) - l2db (kneeLin)) / ratio);
    }
    void solve() {
        lin = db2l (thr); kneeLin = db2l (thr + knee);
        const double slope = 1 / ratio; double lo = 0.001, hi = 1e6;
        for (int it = 0; it < 60; ++it) { // find k so that the curve's slope at the knee matches the compression slope
            const double k = std::sqrt (lo * hi); const double y0 = kneeCurve (kneeLin, k), y1 = kneeCurve (kneeLin * 1.001, k);
            const double s = (l2db (y1) - l2db (y0)) / (l2db (kneeLin * 1.001) - l2db (kneeLin));
            if (s > slope) lo = k; else hi = k;
        }
        kk = std::sqrt (lo * hi); yk = l2db (kneeCurve (kneeLin, kk));
        const double full = sat (1.0); makeup = std::pow (1.0 / std::max (full, 1e-6), 0.6);
        g = 1;
    }
};

// ---- smoothing helper (stands in for setTargetAtTime) ----
struct Smooth {
    double v = 0;
    inline void to (double target, double dt, double tau) { v += (target - v) * (1 - std::exp (-dt / tau)); }
};

// ---- FlutterVoice (new shelf algorithm, ported from flutter.js) ----
class FlutterVoice {
public:
    struct Params { double f0 = 440, split = .14, zeta = .004, u = 0, amp = .5, level = .3, pan = 0; } p;
    double a = 0, va = 0, b = 0, vb = 0, uS = 0, f0S = 440, lvS = 0, sr = 48000;
    void init (double s) { sr = s; reset(); }
    void reset() { a = va = b = vb = uS = lvS = 0; f0S = p.f0; }
    static double cents (double u) {
        static const double T[6][2] = { { 1, 420 }, { 1.2, 560 }, { 1.5, 770 }, { 2, 1220 }, { 3, 2350 }, { 4, 3600 } };
        if (u >= 4) return 3600 + (u - 4) * 1250;
        for (int i = 1; i < 6; ++i) if (u <= T[i][0]) { const double f = (u - T[i - 1][0]) / (T[i][0] - T[i - 1][0]); return T[i - 1][1] + f * (T[i][1] - T[i - 1][1]); }
        return 3600;
    }
    void strike (double vel = 1) { va += vel * p.amp * TAU * p.f0; }
    bool quiet() const { return p.u <= 0 && std::abs (a) + std::abs (b) < 1e-6 && lvS < 1e-5; }
    void process (double* L, double* R, int n) {
        const double dt = 1 / sr, A = p.amp, pl = std::cos ((p.pan + 1) * PI / 4), pr = std::sin ((p.pan + 1) * PI / 4);
        for (int i = 0; i < n; ++i) {
            uS += (p.u - uS) * .002; f0S += (p.f0 - f0S) * .01; lvS += (p.level - lvS) * .004;
            const double u = uS, x = p.split * std::sqrt (std::max (0.0, u * u - 1)), comp = u > 1 ? std::sqrt ((std::sqrt (1 + x * x) + 1) / 2) : 1;
            const double w0 = TAU * f0S / comp * (u > 1 ? std::pow (2.0, cents (u) * p.split * p.split / 1200) : 1), w02 = w0 * w0;
            const double w1s = w02 * (1 - p.split), w2s = w02 * (1 + p.split), c = u * p.split * w02 / (1 + (a * a + b * b) / (A * A));
            const double w1 = std::sqrt (w1s), w2 = std::sqrt (w2s), d1 = 2 * p.zeta * w1, d2 = 2 * p.zeta * w2;
            va += dt * (-w1s * a - d1 * va - c * b);
            vb += dt * (-w2s * b - d2 * vb + c * a);
            a += dt * va; b += dt * vb;
            if (u > 1 && std::abs (a) + std::abs (b) < 1e-5) va += 1e-3 * (rnd() - .5) * w0 * A;
            const double y = a / A * lvS;
            L[i] += y * pl; R[i] += y * pr;
        }
    }
};

// ---- SwarmBank (new shelf algorithm, ported from swarm.js): Kuramoto oscillators at audio rate ----
class SwarmBank {
public:
    struct Params { double n = 12, f0 = 300, spread = .015, k = .8, level = .3, width = .7; } p;
    double r = 0;
    static constexpr int M = 32;
    void init (double s) {
        sr = s;
        for (int i = 0; i < M; ++i) { off[i] = std::fmod (i * .6180339887, 1.0) * 2 - 1; th[i] = std::fmod (i * 2.399963, TAU); wt[i] = 0; }
        f0S = p.f0; lvS = 0; started = false; r = 0;
    }
    void process (double* L, double* R, int n) {
        const int Nt = std::max (1, std::min (M, (int) std::lround (p.n)));
        const double dt = 1 / sr;
        if (! started) { for (int i = 0; i < M; ++i) wt[i] = i < Nt ? 1 : 0; started = true; }
        for (int i = 0; i < M; ++i) { const double pan = p.width * (std::fmod (i * .381966, 1.0) * 2 - 1), a = (pan + 1) * PI / 4; gl[i] = std::cos (a); gr[i] = std::sin (a); }
        int NA = 0; for (int i = 0; i < M; ++i) if (wt[i] > 1e-3 || i < Nt) NA = i + 1;
        for (int i = 0; i < n; ++i) {
            f0S += (p.f0 - f0S) * .0008; lvS += (p.level - lvS) * .002;
            const double w0 = TAU * f0S, gam = w0 * p.spread, Kc = 4 * gam / PI, K = p.k * 2 * Kc;
            double Sc = 0, Ss = 0, W = 0;
            for (int j = 0; j < NA; ++j) { wt[j] += ((j < Nt ? 1 : 0) - wt[j]) * .0006; s[j] = std::sin (th[j]); c[j] = std::cos (th[j]); Sc += wt[j] * c[j]; Ss += wt[j] * s[j]; W += wt[j]; }
            W = std::max (W, 1e-6); r = std::sqrt (Sc * Sc + Ss * Ss) / W;
            const double norm = 1 / std::pow (W, .75); double yl = 0, yr = 0;
            for (int j = 0; j < NA; ++j) {
                const double y = wt[j] * (s[j] + .35 * 2 * s[j] * c[j]);
                yl += y * gl[j]; yr += y * gr[j];
                th[j] += (w0 * (1 + p.spread * off[j]) + K * (Ss * c[j] - Sc * s[j]) / W) * dt;
                if (th[j] > TAU) th[j] -= TAU; else if (th[j] < 0) th[j] += TAU;
            }
            L[i] += yl * norm * lvS; R[i] += yr * norm * lvS;
        }
    }
private:
    double sr = 48000, th[M], off[M], gl[M], gr[M], s[M], c[M], wt[M], f0S = 300, lvS = 0; bool started = false;
};

} // namespace wm
