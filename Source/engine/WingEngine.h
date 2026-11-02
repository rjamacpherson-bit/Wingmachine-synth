// Wing Machine engine (C++ port of the web instrument, v8.2): wings, creatures, behaviors, seasons, environment, wildcards, and the FX chain
// built from the Feather Shelf. No JUCE dependency: the plugin wrapper calls prepare() and process().
#pragma once
#include "Dsp.h"
#include "Creatures.h"
#include "DubCore.h"
#include "XSpace.h"
#include "Stretch.h"
#include "CrushCore.h"
#include "ReverbCore.h"
#include <deque>
#include <memory>
#include <cstring>
#include <cstdio>
#include <atomic>

namespace wm {

struct Settings {
    int creature = 0, mode = 0, season = 0, weather = 0; bool events[9] = {};
    double merge = .45, flange = .4, stretch = .35, sspeed = .5; int smode = 4;
    double dubTrim = 0, dfbTrim = 0, echoTrim = 0, airTrim = 0, roomTrim = 0;
    double tame = .6, mod = .7, slevel = .7, seek = .8, elevel = .8, alt = .5, sizefx = 1.3, quant = .8, swing = .5, block = .6, coupling = 1, outGain = .7;
};
struct HostInfo { double bpm = 120; double ppq = 0; bool playing = false; bool hasPpq = false; };

enum Wx { WX_CLEAR, WX_HEADWIND, WX_TAILWIND, WX_RAIN, WX_STORM, WX_FOG, WX_COLD, WX_HEAT };
enum Sea { SE_NONE, SE_SWARM, SE_MIGRATE, SE_PILLAGE, SE_MATING, SE_BLOOM };
enum { K_WIND, K_RAIN, K_FOG, K_TEMP, K_SMOKE, K_BURN, K_ASH, K_SHAKE, K_RUMBLE, K_OIL, K_TX, K_TB, K_GL, K_STORM, K_MAG, K_RF, K_PANIC, NK };

class WingEngine {
public:
    // ---------------------------------------------------------------- setup
    WingEngine() = default;
    void prepare (double sampleRate) {
        sr = sampleRate;
        noise.make ((int) (sr * 1.5), 1, true); bed.make ((int) (sr * 4), .3, false);
        DB.reset (new pm::DubCore (sr)); DB->p.lock = 1; DB->p.tone = 4200; DB->p.seed = 11; DB->setSeed (11);
        XS.reset (new pm::XSpace()); XS->init (sr);
        ST.reset (new pm::Stretch()); ST->init (sr); ST->p.mix = 1; ST->p.jitter = .25; ST->p.grainMs = 140;
        RV.reset (new pm::ReverbCore (sr)); RVE.reset (new pm::ReverbCore (sr)); CKR.reset (new pm::CrushCore (sr));
        RVE->p.algo = 2; RVE->p.decay = 1.4; RVE->p.size = .6; RVE->update();
        busComp.prepare (sr); busComp.set (-22, 8, 12, .002, .14); mastComp.prepare (sr); mastComp.set (-20, 30, 4, .003, .2);
        for (int i = 0; i < 7; ++i) { FV[i].init (sr); FV[i].p.f0 = i ? 440 : 330; FV[i].f0S = FV[i].p.f0; }
        SWM.init (sr); SWM.p.n = 10;
        for (int ch = 0; ch < 2; ++ch) { dlyBuf[ch].assign ((size_t) (sr * .03) + 8, 0.f); dlyW[ch] = 0; }
        for (int i = 0; i < 2; ++i) { bpA[i].clear(); bpB[i].clear(); lpF[i].clear(); lpF[i].set (Biquad::LP, 4500, 0, sr); envLP[i].clear(); elp[i].clear(); elp[i].set (Biquad::LP, 5500, .5, sr); }
        bedF[0].set (Biquad::BP, 500, .9, sr); bedF[1].set (Biquad::BP, 1800, .5, sr); bedF[2].set (Biquad::LP, 380, .7, sr); bedF[3].set (Biquad::LP, 110, .7, sr); bedF[4].set (Biquad::BP, 1500, .4, sr);
        for (int i = 0; i < 5; ++i) bedPos[i] = rnd() * 3 * sr;
        for (auto& v : vox) v.on = false;
        wings[0] = mkWing (-1); wings[1] = mkWing (1);
        for (int i = 0; i < 4; ++i) feathers[i] = mkFeather (i);
        ready = true; applyCreature (0); applyMode (0); setSeason (SE_NONE);
    }

    // ---------------------------------------------------------------- main entry
    void process (const Settings& s, const HostInfo& h, float* outL, float* outR, int n) {
        if (! ready) { std::memset (outL, 0, sizeof (float) * (size_t) n); std::memset (outR, 0, sizeof (float) * (size_t) n); return; }
        sync (s, h);
        int pos = 0;
        while (pos < n) {
            const int m = std::min (SUB, n - pos);
            if (hostPlaying) beatPos = hostPpq + (double) pos / spb(); // follow the host grid
            sub (m, outL + pos, outR + pos);
            if (! hostPlaying) beatPos += m / spb();
            pos += m;
        }
        if (++statusCnt >= 24) { statusCnt = 0; writeStatus(); }
    }
    void getStatus (char* dst, size_t cap) const { const int i = statusIdx.load(); std::snprintf (dst, cap, "%s", statusBuf[i]); }

    double lastFusion() const { return SWM.r; }

private:
    // ---------------------------------------------------------------- constants and state
    static constexpr int SUB = 64;
    double sr = 48000; bool ready = false;
    std::unique_ptr<pm::DubCore> DB; std::unique_ptr<pm::XSpace> XS; std::unique_ptr<pm::Stretch> ST; std::unique_ptr<pm::ReverbCore> RV, RVE; std::unique_ptr<pm::CrushCore> CKR;
    Settings S; double bpm = 120; double beatPos = 0, hostPpq = 0; bool hostPlaying = false;
    double nowS = 0; // absolute sample position of the current sub-block start
    double spb() const { return sr * 60 / bpm; }
    double bd() const { return 60 / bpm; }

    struct Par { double rate = 3.2, gamma = PI, coup = .6, detune = .02, lag = 1, amp = 1, jitter = .03, meter = 4, burst = 6, bmul = 2, split = .14, ring = .5, vlevel = .5, room = .15, dub = .25, dfb = .45, echo = .35, air = .25; bool gate = true, restGlide = true; int dmode = 1; } P;
    struct Mode { double rate, gamma, coup, detune, lag, amp, jitter, burst; bool gate; double Lamp, Llag; };
    int mode = 0; double modeT = 0, nextChange = 0, threatT = 0; bool tidalOpen = true;
    static const Mode& MODE (int i) {
        static const Mode M[5] = { { 3.2, PI, .6, .02, 1, 1, .03, 6, true, 1, 1 }, { 2.4, .2, .8, .01, 1, 1, .02, 6, false, 1, 1 }, { 5, PI / 2, .5, .04, .8, 1, .1, 5, true, 1, 1 },
                                   { 2.4, 2.1, .15, .07, 1.2, 1, .05, 5, true, .45, 1.7 }, { 6.2, PI, .7, .02, .5, .3, .04, 6, false, 1, 1 } };
        return M[i];
    }
    static constexpr double MULS[4] = { 1, 1.5, 2, 3 };

    // creature
    Derived CR; bool hasCR = false; const std::vector<double>* SCALE = nullptr; double ROOT = 220, QUIET = 1, KMUL = 1, KDIV = 1; std::vector<double> FLANGE;
    double pf (int i) const { const int n = (int) SCALE->size(); return ROOT * (*SCALE)[(size_t) (((i % n) + n) % n)] * std::pow (2.0, std::floor ((double) i / n)); }
    double pfS (int i, const std::vector<double>& arr) const { const int n = (int) arr.size(); return ROOT * arr[(size_t) (((i % n) + n) % n)] * std::pow (2.0, std::floor ((double) i / n)); }
    void buildFlange() { FLANGE.clear(); for (int o = -3; o <= 3; ++o) for (double r : *SCALE) { const double f = ROOT * r * std::pow (2.0, o); if (f >= 140 && f <= 900) FLANGE.push_back (f); } std::sort (FLANGE.begin(), FLANGE.end()); if (FLANGE.empty()) FLANGE.push_back (440); }

    // wings
    struct Wing { int side; double th, env = 0, ampMul = 1, lagMul = 1, yPrev = 0, U = 0; bool sing = false; long cnt[4]; bool cntInit = false; double jit[4] = { 0, 0, 0, 0 }; long clapN = -1000000000L; };
    Wing wings[2];
    static Wing mkWing (int side) { Wing w; w.side = side; w.th = side < 0 ? .3 : .3 + PI; for (auto& c : w.cnt) c = 0; return w; }
    struct EvDef { int k; double ph; }; // 0 thump, 1 tick, 2 blipD, 3 blipU
    static const EvDef& EVD (int i) { static const EvDef E[4] = { { 0, PI / 2 }, { 1, PI / 2 + .9 }, { 2, PI + 1.8 }, { 3, 1.8 } }; return E[i]; }
    double Eg = .2; bool gateOpen = true; long lastCycL = 0; bool lastCycInit = false; double breath = 0; long breathCnt = 0; bool accent = false;
    struct Joint { double s, e, r, tip, A; };
    Joint joint (const Wing& w) const {
        const double a = w.th, lag = P.lag * w.lagMul, A = w.env * w.ampMul * P.amp * ENV.ampMul * WILD.ampMul;
        const double s = std::cos (a), e = std::cos (a - .9 * lag), r = std::cos (a - 1.8 * lag);
        return { s * A, e * A, r * A, (.5 * s + .32 * e + .18 * r) * A, A };
    }

    // feather modulator
    struct Feather { double x, y, vx, vy, ph; bool rest; double ang; };
    Feather feathers[4]; double modH = 0, modSway = 0, modSettle = 1;
    static Feather mkFeather (int i) { return { 50 + i * 90 + rnd() * 30, 150 - 8, 0, 0, rnd() * TAU, true, PI / 2 }; }
    void lift (double strength) { for (auto& f : feathers) { const double s = strength * (.6 + .8 * rnd()); f.rest = false; f.vy -= 30 * s; f.vx += (rnd() - .5) * 36 * s; } }
    void stepFeathers (double dt) {
        const double FW = 360, FH = 150, FG = 130, FK = .012; double hs = 0, sw = 0, rs = 0;
        for (auto& f : feathers) {
            if (f.rest) { rs++; continue; }
            const double sp = std::sqrt (f.vx * f.vx + f.vy * f.vy); f.ph += dt * TAU * (.5 + sp / 120);
            const double ax = -FK * sp * f.vx + 160 * std::min (1.0, sp / 70) * std::sin (f.ph), ay = FG - FK * sp * f.vy;
            f.vx += ax * dt; f.vy += ay * dt; f.x += f.vx * dt; f.y += f.vy * dt;
            if (f.x < 8) { f.x = 8; f.vx = std::abs (f.vx) * .5; } if (f.x > FW - 8) { f.x = FW - 8; f.vx = -std::abs (f.vx) * .5; }
            if (f.y < 8) { f.y = 8; f.vy = std::abs (f.vy) * .4; } if (f.y >= FH - 8) { f.y = FH - 8; f.vx = 0; f.vy = 0; f.rest = true; }
        }
        for (auto& f : feathers) { hs += 1 - (f.y - 8) / (FH - 16); sw += std::min (1.0, std::abs (f.vx) / 40); }
        const double h = clampd (hs / 4 - .02, 0, 1);
        modH += (h - modH) * std::min (1.0, dt * 8); modSway += (sw / 4 - modSway) * std::min (1.0, dt * 6); modSettle = rs / 4;
    }
    double modMix (double base, double mv) const { return base + S.mod * (mv - base); }

    // ---------------------------------------------------------------- sound voices
    enum { V_KICK, V_BLIP, V_NOISE, V_TONE };
    struct Vox { bool on = false; int type = 0, bus = 0; double start = 0, a = .003, d = .1, peak = 0, f = 0, f1 = 0, pan = 0, ph = 0, ph2 = 0, endT = 0; Biquad bq; size_t np = 0; };
    Vox vox[72]; int active = 0;
    PinkBuf noise, bed;
    Vox* alloc() { for (auto& v : vox) if (! v.on) return &v; return nullptr; }
    int countActive (int bus) const { int c = 0; for (auto& v : vox) if (v.on && v.bus == bus) ++c; return c; }
    void spawnNoise (Biquad::Type ty, double f, double q, double dur, double amp, double tAbs, int bus, double pan) {
        if (ty == Biquad::HP) { ty = Biquad::BP; f = std::min (f * .55, 3600.0); q = std::min (q, .6); }
        if (bus == 0) amp *= QUIET;
        if (countActive (bus) > (bus == 0 ? 36 : 40)) return; Vox* v = alloc(); if (! v) return;
        v->on = true; v->type = V_NOISE; v->bus = bus; v->start = tAbs; v->a = .003; v->d = dur; v->peak = amp; v->pan = pan; v->bq.clear(); v->bq.set (ty, f, q, sr);
        v->np = (size_t) (rnd() * (double) noise.d.size()); v->endT = tAbs + (dur + .02) * sr;
    }
    void sndNoiseHit (double f, double q, double dur, double amp, Biquad::Type ty, double tAbs) { spawnNoise (ty, f, q, dur, amp, tAbs, 0, 0); }
    void nburst (Biquad::Type ty, double f, double q, double dur, double amp, double tAbs, double pan = 0) { spawnNoise (ty, f, q, dur, amp, tAbs, 1, pan); }
    void toneEnv (double f, double dur, double amp, double tAbs, double pan = 0) {
        if (countActive (1) > 40) return; Vox* v = alloc(); if (! v) return;
        v->on = true; v->type = V_TONE; v->bus = 1; v->start = tAbs; v->a = .002; v->d = dur; v->peak = amp; v->f = f; v->pan = pan; v->ph = 0; v->endT = tAbs + (dur + .02) * sr;
    }
    void sndKick (const Wing& w, double vel, bool acc, double tAbs) {
        if (countActive (0) > 36) return; Vox* v = alloc(); if (! v) return;
        double f0 = (w.side < 0 ? 140 : 175) * (acc ? .85 : 1), f1 = w.side < 0 ? 48 : 60;
        vel *= QUIET * KMUL; f0 = std::max (60.0, f0 / KDIV); f1 = std::max (30.0, f1 / KDIV);
        vel *= .4; f0 *= 1.5; f1 *= 1.5; // over the host's drums the wing thump is a light tom, not a second kick
        v->on = true; v->type = V_KICK; v->bus = 0; v->start = tAbs; v->a = .004; v->d = .2; v->peak = .55 * vel * (acc ? 1.35 : 1); v->f = f0; v->f1 = f1; v->ph = 0; v->pan = 0; v->endT = tAbs + .25 * sr;
    }
    void sndTick (const Wing& w, double vel, double tAbs) { sndNoiseHit (w.side < 0 ? 5200 : 7200, .8, .03, .2 * vel, Biquad::HP, tAbs); }
    void sndBlip (double freq, double vel, double tAbs) {
        if (countActive (0) > 36) return; Vox* v = alloc(); if (! v) return;
        v->on = true; v->type = V_BLIP; v->bus = 0; v->start = tAbs; v->a = .003; v->d = .13; v->peak = .28 * vel; v->f = freq; v->ph = 0; v->ph2 = 0; v->pan = 0; v->endT = tAbs + .2 * sr;
    }
    void sndClap (double tAbs) { sndNoiseHit (2800, .7, .04, .3, Biquad::BP, tAbs); sndNoiseHit (2200, .7, .035, .2, Biquad::BP, tAbs + .035 * sr); }
    static double envAmp (double e, double a, double peak, double d) { if (e < 0) return 0; if (e < a) return peak * e / a; if (e >= d) return peak * 1e-4 / std::max (peak, 1e-9); return peak * std::pow (1e-4 / std::max (peak, 1e-9), (e - a) / (d - a)); }
    void renderVox (int n, double* sL, double* sR, double* eL, double* eR) {
        for (auto& v : vox) {
            if (! v.on) continue;
            if (nowS >= v.endT) { v.on = false; continue; }
            double* oL = v.bus == 0 ? sL : eL; double* oR = v.bus == 0 ? sR : eR;
            const double gl = std::cos ((v.pan + 1) * PI / 4), gr = std::sin ((v.pan + 1) * PI / 4);
            for (int i = 0; i < n; ++i) {
                const double e = (nowS + i - v.start) / sr; if (e < 0) continue; if (e > v.d + .25) break;
                double y = 0;
                if (v.type == V_KICK) { const double fq = e < .12 ? v.f * std::pow (v.f1 / v.f, e / .12) : v.f1; v.ph += TAU * fq / sr; y = std::sin (v.ph) * envAmp (e, v.a, v.peak, v.d); }
                else if (v.type == V_BLIP) {
                    const double fc = e < .02 ? v.f * 1.3 * std::pow (1 / 1.3, e / .02) : v.f, mg = e < .1 ? v.f * 1.6 * std::pow (1 / (v.f * 1.6), e / .1) : 1;
                    v.ph2 += TAU * v.f * 2 / sr; v.ph += TAU * (fc + mg * std::sin (v.ph2)) / sr; y = std::sin (v.ph) * envAmp (e, v.a, v.peak, v.d);
                } else if (v.type == V_TONE) { const double fq = e < .02 ? v.f * 1.05 * std::pow (1 / 1.05, e / .02) : v.f; v.ph += TAU * fq / sr; y = std::sin (v.ph) * envAmp (e, v.a, v.peak, v.d); }
                else { y = v.bq.process (noise.d[v.np++ % noise.d.size()]) * envAmp (e, v.a, v.peak, v.d); }
                oL[i] += y * (v.bus == 1 ? gl : 1); oR[i] += y * (v.bus == 1 ? gr : 1);
            }
        }
    }

    // ---------------------------------------------------------------- strikes for the feather voices
    struct Strike { double t; int v; double f0, vel; };
    std::deque<Strike> strikeQ; FlutterVoice FV[7]; SwarmBank SWM;
    void pushStrike (Strike e) { auto it = strikeQ.end(); while (it != strikeQ.begin() && (it - 1)->t > e.t) --it; strikeQ.insert (it, e); }

    // ---------------------------------------------------------------- timing against the host grid
    double slotBeat (long k) const { return k * .25 + ((k & 1) ? (S.swing - .5) * .5 : 0); }
    double laNow() const { return S.quant > 0 ? .15 * bd() * S.quant + .02 : 0; } // seconds
    // sample time for an event happening now, pulled toward the nearest swung 16th of the host grid
    double evTime() const {
        const double now = nowS; if (S.quant <= 0) return now;
        const double T = now + laNow() * sr, bT = beatPos + (T - now) / spb(); const long k = (long) std::llround (bT / .25);
        double best = slotBeat (k), bdst = std::abs (best - bT);
        for (long j = k - 1; j <= k + 1; j += 2) { const double sl = slotBeat (j), dd = std::abs (sl - bT); if (dd < bdst) { bdst = dd; best = sl; } }
        const double slotS = now + (best - beatPos) * spb();
        return std::max (now + .005 * sr, T + S.quant * (slotS - T));
    }
    double RATE() const { return (bpm / 60) * MULS[(int) P.bmul] * (P.rate / 3.2) * SEA.rateMul * ENV.rateMul * WILD.rateMul; }
    double LOCKM() const { return clampd (1 - .8 * ENV.gl - .6 * ENV.rf - .3 * ENV.fog, 0, 1); }

    // ---------------------------------------------------------------- environment
    struct EnvState {
        double v[NK] = {}; // targets smoothed in place: wind rain fog temp smoke burn ash shake rumble oil tx tb gl storm mag rf panicIn
        double rateMul = 1, ampMul = 1, jit = 0, ringMul = 1, lvl = 1, coh = 1, swm = 1, muffle = 0, crush = 0, damp = 0, decayMul = 1, prog = 1, panic = 0;
        double gust = 0, gustTgt = 0, gustT = 0, walk = 0, walkV = 0, keyWalk = 0, silence = 0, thunderT = 6, flash = 0, tremT = 2, circ = 0, exh = 0, wob = 0;
        bool forced = false; int prevMode = 0, base = 0; struct Ev { bool on = false; double t = 0, i = 0; } ev[9];
    } ENV0;
    // reference-style accessors (written as ENV.name below)
    struct EnvView {
        EnvState& e;
        double &wind, &rain, &fog, &temp, &smoke, &burn, &ash, &shake, &rumble, &oil, &tx, &tb, &gl, &storm, &mag, &rf, &panicIn;
        double &rateMul, &ampMul, &jit, &ringMul, &lvl, &coh, &swm, &muffle, &crush, &damp, &decayMul, &prog, &panic, &gust, &walk, &walkV, &keyWalk, &silence, &flash, &exh;
        explicit EnvView (EnvState& s) : e (s), wind (s.v[K_WIND]), rain (s.v[K_RAIN]), fog (s.v[K_FOG]), temp (s.v[K_TEMP]), smoke (s.v[K_SMOKE]), burn (s.v[K_BURN]), ash (s.v[K_ASH]), shake (s.v[K_SHAKE]), rumble (s.v[K_RUMBLE]), oil (s.v[K_OIL]),
            tx (s.v[K_TX]), tb (s.v[K_TB]), gl (s.v[K_GL]), storm (s.v[K_STORM]), mag (s.v[K_MAG]), rf (s.v[K_RF]), panicIn (s.v[K_PANIC]), rateMul (s.rateMul), ampMul (s.ampMul), jit (s.jit), ringMul (s.ringMul), lvl (s.lvl), coh (s.coh), swm (s.swm),
            muffle (s.muffle), crush (s.crush), damp (s.damp), decayMul (s.decayMul), prog (s.prog), panic (s.panic), gust (s.gust), walk (s.walk), walkV (s.walkV), keyWalk (s.keyWalk), silence (s.silence), flash (s.flash), exh (s.exh) {}
    };
    EnvView ENV { ENV0 };
    struct EventDef { const char* name; double len; int nk; int key[4]; double val[4]; };
    static const EventDef& EVDEF (int i) {
        static const EventDef D[9] = {
            { "Wildfire", 55, 4, { K_BURN, K_SMOKE, K_TEMP, K_PANIC }, { 1, .8, .6, .8 } }, { "Ash fall", 55, 4, { K_ASH, K_SMOKE, K_FOG, K_RUMBLE }, { 1, .4, .4, .5 } },
            { "Tremor", 26, 3, { K_SHAKE, K_RUMBLE, K_PANIC }, { 1, .8, 1 } }, { "Oil spill", 70, 1, { K_OIL }, { 1 } }, { "Pesticide", 70, 1, { K_TX }, { 1 } },
            { "Turbines", 70, 2, { K_TB, K_WIND }, { 1, .35 } }, { "City lights", 70, 2, { K_GL, K_FOG }, { 1, .25 } }, { "Magnetic storm", 60, 1, { K_MAG }, { 1 } }, { "Radio noise", 70, 2, { K_RF, K_GL }, { 1, .3 } } };
        return D[i];
    }
    double envAsh5 = 0;
    // beds
    Smooth bedG[5], humG, auG, auF[3], windF; double humPh[3] = {}, auPh[3] = {}, auLfo = 0; size_t bedPos[5]; Biquad bedF[5]; Biquad elp[2]; 

    double KW() const { return std::pow (2.0, ENV.keyWalk / 12); }
    double curRoot() const { return (hasCR ? CR.root : 220) * SEA.keyMul * KW() * WILD.keyMul; }
    double zetaR() const { return .0015 + .011 * (1 - clampd (P.ring * ENV.ringMul * WILD.ringMul, 0, 1)); }
    double ENVDET() const { return (.06 * ENV.tx + .03 * ENV.gl + .02 * ENV.fog + .04 * ENV.rf + .02 * ENV.mag) * ENV.walk + WILD.det; }
    bool isInsect() const { return hasCR && (CR.special == SP_BUZZ || CR.special == SP_WARM || CR.special == SP_SCALES || CR.special == SP_PLAGUE || CR.special == SP_FLASH || std::strcmp (CR.c->id, "bee") == 0 || std::strcmp (CR.c->id, "dragonfly") == 0); }
    void setWeather (int w) { ENV.e.base = w; }
    void setEvent (int i, bool on) { if (on && ! ENV.e.ev[i].on) { ENV.e.ev[i].on = true; ENV.e.ev[i].t = 0; ENV.e.ev[i].i = 0; } else if (! on) ENV.e.ev[i].on = false; }
    void thunderStorm() { const double t = nowS + .02 * sr; ENV.flash = 1; nburst (Biquad::HP, 1500, .7, .18, .22, t); nburst (Biquad::LP, 170, .7, 3, .7, t + .06 * sr); toneEnv (46, 2, .35, t + .05 * sr); DB->throwNow (5); }
    void whoosh (double t) {
        // a turbine blade passing: a swept band with a pan from left to right (approximated as two bursts)
        nburst (Biquad::BP, 500, 1.1, .9, .3 * ENV.tb, t, -.7); nburst (Biquad::BP, 900, 1.1, .5, .2 * ENV.tb, t + .3 * sr, .7);
        if (rnd() < .16 * ENV.tb) { ENV.silence = std::max (ENV.silence, .4 * bd()); toneEnv (70, .4, .25, t + .4 * sr); }
    }
    void circleLight (double t) { static const int deg[6] = { 0, 2, 4, 6, 4, 2 }; const int d = deg[(int) ENV.e.circ++ % 6]; pushStrike ({ t, (int) ENV.e.circ % 2, foldHz (pf (d + (int) SCALE->size()), 200, 1100), .4 * ENV.gl * (1 - .7 * ENV.exh) }); }
    void envStep (double dt) {
        double tgt[NK] = {};
        switch (ENV.e.base) {
            case WX_HEADWIND: tgt[K_WIND] += .8; break; case WX_TAILWIND: tgt[K_WIND] += -.8; break;
            case WX_RAIN: tgt[K_RAIN] += .6; tgt[K_WIND] += .2; break; case WX_STORM: tgt[K_RAIN] += .9; tgt[K_WIND] += .8; tgt[K_STORM] += 1; tgt[K_PANIC] += .6; break;
            case WX_FOG: tgt[K_FOG] += .8; break; case WX_COLD: tgt[K_TEMP] += -.8; break; case WX_HEAT: tgt[K_TEMP] += .8; break; default: break; }
        for (int id = 0; id < 9; ++id) { auto& e = ENV.e.ev[id]; if (! e.on) continue; const EventDef& d = EVDEF (id); e.t += dt; if (e.t >= d.len) { e.on = false; continue; }
            e.i = std::max (0.0, std::min ({ 1.0, e.t / 6, (d.len - e.t) / 12 })); for (int j = 0; j < d.nk; ++j) tgt[d.key[j]] += d.val[j] * e.i; }
        const double lvl = S.elevel, sm = 1 - std::exp (-dt / 2.2);
        for (int k = 0; k < NK; ++k) { double v = tgt[k] * lvl; v = (k == K_WIND || k == K_TEMP) ? clampd (v, -1, 1) : clampd (v, 0, 1); ENV.e.v[k] += (v - ENV.e.v[k]) * sm; }
        auto& E = ENV.e;
        E.gustT -= dt; if (E.gustT <= 0) { E.gustTgt = rnd(); E.gustT = 1 + rnd() * 2.5; } E.gust += (E.gustTgt - E.gust) * (1 - std::exp (-dt / .6));
        const double nz = std::sqrt (dt * (1.0 / 60)); // keeps the random walks' character independent of the control rate
        E.walkV += ((rnd() - .5) * 6 * nz) - E.walkV * dt * .9; E.walk = clampd (E.walk + E.walkV * dt, -1, 1);
        const double lim = 1.2 * ENV.mag + 2.4 * ENV.rf; E.keyWalk += E.walkV * dt * (1.5 + 3 * lim) * (lim > .02 ? 1 : 0); E.keyWalk = clampd (E.keyWalk, -lim, lim); if (lim <= .02) E.keyWalk *= 1 - std::min (1.0, dt * .8);
        const double ins = isInsect() ? 1 : 0, cold = std::max (0.0, -ENV.temp), hot = std::max (0.0, ENV.temp), hw = std::max (0.0, ENV.wind), tw = std::max (0.0, -ENV.wind);
        E.rateMul = clampd (1 - (ins ? .25 : .07) * ENV.rain - .2 * ENV.oil - .1 * ENV.smoke - (ins ? .25 * cold + .08 * hot : -.05 * cold + .04 * hot), .4, 1.2);
        E.ampMul = clampd (1 + .35 * hw * (1 - .7 * ENV.mag) - .2 * tw - .35 * ENV.oil - (ins ? .3 : .15) * ENV.rain - .2 * ENV.smoke - .1 * ENV.ash, .3, 1.6);
        E.jit = .08 * std::abs (ENV.wind) * (.5 + E.gust) + .05 * ENV.rain + .1 * ENV.fog + .12 * ENV.tx + .15 * ENV.gl + .2 * ENV.shake + .05 * ENV.storm + .08 * ENV.mag + .12 * ENV.rf;
        E.ringMul = clampd (1 - .5 * ENV.rain - .6 * ENV.oil - .3 * ENV.smoke - .3 * ENV.ash, .15, 1);
        E.lvl = clampd (1 - .4 * ENV.smoke - .3 * ENV.fog - .5 * ENV.oil - .3 * ENV.ash - (ins ? .3 * cold : 0), .2, 1);
        E.coh = clampd (1 - .6 * ENV.fog - .5 * ENV.tx - .5 * ENV.gl - .3 * ENV.storm - .4 * ENV.smoke - .35 * ENV.mag - .8 * ENV.rf, .1, 1);
        E.swm = clampd (1 - (ins ? .9 : .3) * ENV.rain - .5 * ENV.tx - .4 * ENV.oil - .3 * ENV.ash - .15 * ENV.mag, .05, 1);
        E.muffle = clampd (.6 * ENV.fog + .5 * ENV.smoke + .8 * ENV.ash + .5 * ENV.oil + .25 * ENV.rain, 0, 1);
        E.crush = clampd (.5 * ENV.ash + .3 * ENV.burn + .15 * ENV.shake, 0, 1);
        E.damp = .3 * ENV.rain + .2 * ENV.fog; E.decayMul = 1 + .5 * ENV.fog + .2 * ENV.ash;
        E.prog = clampd (1 - .6 * hw + .5 * tw - .4 * ENV.fog - .3 * ENV.rain - .3 * ENV.mag, .2, 1.6);
        E.panic = std::max ({ ENV.panicIn, ENV.shake, .8 * ENV.burn, .6 * ENV.storm, .5 * ENV.ash });
        if (E.panic > .5 && ! E.forced) { E.prevMode = mode; E.forced = true; if (mode != 2) applyMode (2); }
        else if (E.panic < .2 && E.forced) { E.forced = false; applyMode (E.prevMode); }
        if (hasCR) { const double rt = curRoot(); if (std::abs (rt / (lastRoot ? lastRoot : rt) - 1) > .015) { lastRoot = rt; ROOT = rt; buildFlange(); } else ROOT = rt; }
        E.silence = std::max (0.0, E.silence - dt); if (ENV.tx > .1 && rnd() < dt * .6 * ENV.tx) E.silence = .15 + rnd() * .35;
        if (ENV.gl > .25) E.exh = std::min (1.0, E.exh + dt / 45); else E.exh = std::max (0.0, E.exh - dt / 10);
        E.flash = std::max (0.0, E.flash - dt * 2.5); E.wob = std::max (0.0, E.wob - dt * 4);
        envSounds (dt);
    }
    double lastRoot = 0;
    void envSounds (double dt) {
        const double t = nowS + .02 * sr; auto& E = ENV.e;
        if (ENV.rain > .05 && rnd() < dt * 9 * ENV.rain) toneEnv (foldHz (pf ((int) (rnd() * 12)), 900, 3200), .12, .05 * ENV.rain, t, rnd() * 1.6 - .8);
        if ((ENV.burn > .05 || ENV.ash > .05) && rnd() < dt * (24 * ENV.burn + 8 * ENV.ash)) nburst (Biquad::BP, 1500 + rnd() * 3500, 1.2, .012 + rnd() * .02, .1 + .12 * rnd(), t, rnd() * 1.6 - .8);
        E.thunderT -= dt; if (ENV.storm > .3 && E.thunderT <= 0) { thunderStorm(); E.thunderT = 7 + rnd() * 12; }
        E.tremT -= dt; if (ENV.shake > .1 && E.tremT <= 0) { E.tremT = .9 + rnd() * 1.4; E.wob = 1; toneEnv (38, .7, .4 * ENV.shake, t); nburst (Biquad::LP, 120, .7, .6, .35 * ENV.shake, t); if (rnd() < .5) DB->throwNow (5); }
        if (ENV.rf > .05 && rnd() < dt * 30 * ENV.rf) nburst (Biquad::HP, 3000 + rnd() * 4000, .8, .004 + rnd() * .01, .06 * ENV.rf, t, rnd() * 1.6 - .8);
    }
    // continuous beds, rendered into the environment bus
    void renderBeds (int n, double* eL, double* eR) {
        const double dt = (double) n / sr, tt = nowS / sr; const double gust = ENV.gust;
        bedG[0].to (.2 * std::abs (ENV.wind) * (.6 + .8 * gust), dt, .15); bedG[1].to (.14 * ENV.rain, dt, .2);
        bedG[2].to (.24 * (ENV.burn + .3 * ENV.ash) * (.8 + .4 * std::sin (tt * 1.3)), dt, .2); bedG[3].to (.4 * std::max (ENV.rumble, ENV.shake) * (.8 + .3 * std::sin (tt * 7)), dt, .1);
        bedG[4].to (.05 * ENV.rf, dt, .1); windF.to (280 + 900 * (.35 + .65 * gust), dt, .2); bedF[0].set (Biquad::BP, windF.v < 1 ? 500 : windF.v, .9, sr);
        humG.to (.05 * std::max (.8 * ENV.gl, .9 * ENV.rf), dt, .2); auG.to (.045 * ENV.mag, dt, .4);
        for (int i = 0; i < 3; ++i) auF[i].to (foldHz (pf (i * 2 + 3), 1500, 4200) * (1 + .004 * std::sin (tt * (.5 + i * .37))), dt, .1);
        const double lfoT = .5 + .5 * std::sin (tt * .8); auLfo += (lfoT - auLfo) * (1 - std::exp (-dt / .3));
        const bool any = bedG[0].v + bedG[1].v + bedG[2].v + bedG[3].v + bedG[4].v + humG.v + auG.v > 1e-5;
        if (! any) return;
        static const double HUMA[3] = { 1, .5, .3 }; static const double HUMF[3] = { 50, 100, 150 };
        for (int i = 0; i < n; ++i) {
            double y = 0;
            for (int b = 0; b < 5; ++b) { const double x = bed.d[bedPos[b]++ % bed.d.size()]; y += bedF[b].process (x) * bedG[b].v; }
            for (int h = 0; h < 3; ++h) { humPh[h] += TAU * HUMF[h] / sr; y += std::sin (humPh[h]) * HUMA[h] * humG.v; }
            for (int a = 0; a < 3; ++a) { auPh[a] += TAU * auF[a].v / sr; y += std::sin (auPh[a]) * auLfo * auG.v; }
            eL[i] += y; eR[i] += y;
        }
    }

    // ---------------------------------------------------------------- seasons
    struct SeaState {
        int id = SE_NONE; double rateMul = 1, keyMul = 1, F = 1, d = 0, phaseT = 0, prog = 0, fM = 300, fF = 200, recept = 1, holdT = 0, stateT = 0, mu = 0, swLvl = 0, eps = 0, flash = 0;
        bool rest = false; int phase = 0 /*0 feast 1 aftermath 2 regrow*/, state = 0 /*0 court 1 mate 2 rest*/, leg = 0;
    } SEA;
    static constexpr int LEGSEMI = 7;
    void setKey (double mul) { SEA.keyMul = mul; if (hasCR) { ROOT = CR.root * mul; buildFlange(); } }
    int swarmN() {
        int n = (int) clampd (std::round (10 / std::sqrt (hasCR ? CR.M : 1)), 4, 24);
        if (hasCR && CR.special == SP_PLAGUE) n = std::min (32, (int) std::lround (n * 2.2));
        if (hasCR && CR.special == SP_THIRTY) { const double at = clampd (.5 * ENV.panic + .3 * ENV.storm + .3 * ENV.burn + .3 * ENV.tx + .2 * ENV.rf, 0, .85); WILD.attr = at; n = std::max (3, (int) std::lround (30 * (1 - at))); }
        return n;
    }
    int legAlgo (int leg) const { static const int A[4] = { 2, 5, 8, 4 }; return leg % 5 == 0 ? (hasCR ? CR.algo : 2) : A[leg % 5 - 1]; }
    void startCourt() {
        double base = (hasCR && CR.humF > 0) ? CR.humF : foldHz (hasCR ? CR.root : 300, 150, 500); base = snapScale (base * SEA.keyMul, ROOT, *SCALE);
        const double sgn = rnd() < .5 ? -1 : 1; SEA.fF = base; SEA.fM = base * 1.5 * (1 + sgn * (.05 + rnd() * .07)); SEA.recept = 1; SEA.state = 0; SEA.holdT = 0; SEA.stateT = 0; SEA.mu = 1.15;
    }
    void flourish() {
        const double t0 = nowS + .1 * sr, b = bd(); SEA.flash = 1;
        for (int k = 0; k < 8; ++k) pushStrike ({ t0 + k * .125 * b * sr, k % 2, foldHz (pf (k + 3), 150, 1100), .45 });
        sndClap (t0); DB->throwNow (5);
    }
    void setSeason (int id) {
        auto& s = SEA; s.id = id; s.rateMul = 1; s.rest = false; s.mu = 0; s.d = 0; s.F = 1; s.phase = 0; s.phaseT = 0; s.leg = 0; s.prog = 0; s.swLvl = 0;
        if (hasCR) { setKey (1); RV->p.algo = CR.algo; }
        if (id == SE_MATING) startCourt();
        if (id == SE_BLOOM) { BL = Bloom(); }
    }
    void seasonStep (double dt) {
        auto& s = SEA; const double lvl = S.slevel;
        if (s.id == SE_PILLAGE) {
            const double N = SWM.p.n, r = SWM.r;
            if (s.phase == 0) { s.F = std::max (0.0, s.F - dt * .006 * (N / 10) * (.3 + .7 * r) * (.5 + lvl)); if (s.F < .03) { s.phase = 1; s.phaseT = 0; } }
            else if (s.phase == 1) { s.phaseT += dt; if (s.phaseT > 10) s.phase = 2; }
            else { s.F = std::min (1.0, s.F + dt * .03); if (s.F >= .98) s.phase = 0; }
            s.d = 1 - s.F;
        } else if (s.id == SE_MIGRATE) {
            s.prog += dt * ENV.prog / (16 * 4 * bd()); s.rest = s.prog > .88; s.rateMul = s.rest ? 1 : 1 - .2 * s.prog * s.prog;
            if (s.prog >= 1) { s.prog = 0; s.leg++; setKey (std::pow (2.0, (double) ((LEGSEMI * s.leg) % 12) / 12)); RV->p.algo = legAlgo (s.leg); }
        } else if (s.id == SE_MATING) {
            s.stateT += dt; s.flash = std::max (0.0, s.flash - dt * .7);
            if (s.state == 0) {
                s.eps = (s.fM * 2) / (s.fF * 3) - 1; const double a = .06 * s.recept * dt * (.5 + lvl);
                s.fM *= 1 - a * s.eps; s.fF *= 1 + a * s.eps;
                if (std::abs (s.eps) < .002) s.holdT += dt; else s.holdT = 0;
                s.rateMul = 1 + .2 * (1 - std::min (1.0, std::abs (s.eps) * 8)); s.mu = 1.15;
                if (s.holdT > 1.2) { s.state = 1; s.stateT = 0; flourish(); }
            } else if (s.state == 1) { s.mu = 1.4; s.rateMul = 1; if (s.stateT > 6) { s.state = 2; s.stateT = 0; s.recept = 0; } }
            else { s.mu = std::max (0.0, s.mu - dt * .3); s.rateMul = 1; if (s.stateT > 10) startCourt(); }
        } else { s.rateMul = 1; s.rest = false; }
        bloomStep (dt);
    }
    void seasonAudio() {
        auto& s = SEA; const double lvl = S.slevel, d = s.d; const bool on = s.id == SE_SWARM || s.id == SE_PILLAGE; double tgt = on ? lvl : 0;
        if (s.id == SE_PILLAGE && s.phase != 0) tgt *= s.phase == 1 ? std::max (0.0, 1 - s.phaseT / 4) : 0;
        s.swLvl += (tgt - s.swLvl) * .05;
        SWM.p.n = s.id == SE_PILLAGE ? std::min (32, (int) std::lround (swarmN() * 1.5)) : swarmN();
        SWM.p.f0 = ((hasCR && CR.humF > 0) ? CR.humF : foldHz (hasCR ? CR.root : 300, 150, 600)) * s.keyMul * KW() * std::pow (2.0, -3 * d / 12);
        SWM.p.k = S.seek * ENV.coh; SWM.p.level = .34 * s.swLvl * ENV.swm; SWM.p.spread = .015; SWM.p.width = .7;
        if (hasCR && CR.special == SP_THIRTY) SWM.p.k *= clampd ((SWM.p.n - 6) / 10, .2, 1);
        const double EC = std::max (ENV.crush, WILD.crush), dc2 = std::max (d, EC);
        CKR->p.mix = std::max (s.id == SE_PILLAGE ? std::pow (d, .7) * std::min (1.0, lvl * 1.4) : 0, EC); CKR->p.bits = 12 - 5 * dc2; CKR->p.rate = 48000 * (1 - .5 * dc2); CKR->p.tone = 16000; CKR->p.drive = 0;
        RV->p.shimmer = (s.id == SE_MATING && s.state == 1) ? .5 : 0;
        if (s.id == SE_BLOOM) RV->p.shimmer = BL.shim > 0 ? .5 : 0;
    }

    // ---------------------------------------------------------------- wildcards (v8) and Bloom
    struct Wild {
        double rateMul = 1, ampMul = 1, det = 0, crush = 0, muffle = 0, ringMul = 1, keyMul = 1, warm = 0, attr = 0, sil = 0; bool open = true; int reborn = 0;
        struct Fy { double t = 0, wait = 2, dur = 3; bool burst = false, answered = true; int n = 0; } fy;
        struct Ph { int s = 0; double t = 0, beat = 0; } ph; struct Tb { double d = 1, last = 0; } tb;
    } WILD;
    struct Bloom { double F = .3, L = 0, vis = .2, open = 1, shim = 0; int dep = 0; } BL;
    void wildReset() { WILD = Wild(); }
    void wildStep (double dt) {
        auto& W = WILD; const int sp = hasCR ? CR.special : SP_NONE; double rm = 1, am = 1, det = 0, cr = 0, mu = 0, rg = 1; bool op = true;
        W.sil = std::max (0.0, W.sil - dt);
        if (sp == SP_WARM) {
            const double cold = std::max (0.0, -ENV.temp), hot = std::max (0.0, ENV.temp);
            W.warm = clampd (W.warm + ((SEA.rest || ENV.silence > 0) ? -.04 * dt : dt / (9 * (1 + 1.5 * cold) * (1 - .3 * hot))), 0, 1);
            rm = .4 + .6 * W.warm; am = .45 + .55 * W.warm; det = .035 * (1 - W.warm);
        } else if (sp == SP_FLASH) {
            auto& F = W.fy; const double r = (SEA.id == SE_SWARM || SEA.id == SE_PILLAGE) ? SWM.r : 0; F.t += dt;
            if (! F.burst) {
                if (F.t >= F.wait) { F.burst = true; F.t = 0; F.dur = 3 + rnd() * 1.4; F.answered = false; F.n++; }
                else if (! F.answered && F.t >= 2 && F.t - dt < 2) { F.answered = true; toneEnv (foldHz (pf ((int) SCALE->size() + 4) * 1.5, 300, 900), 1.1, .09, nowS + .05 * sr); }
            } else if (F.t >= F.dur) { F.burst = false; F.t = 0; const double per = 12 * (1 - .15 * ENV.temp); F.wait = r * std::max (2.0, per - F.dur) + (1 - r) * (2 + rnd() * 14); }
            op = F.burst;
        } else if (sp == SP_WINDOW) {
            const double low = clampd ((.32 - S.alt) / .32, 0, 1), high = clampd ((S.alt - .68) / .32, 0, 1);
            am = 1 - .55 * low; mu = .7 * low; rg = 1 - .7 * low; rm = 1 - .15 * low; cr = .75 * high; det = .07 * high;
            if (high > .1 && rnd() < dt * 2.2 * high) { W.sil = .12 + rnd() * .25; toneEnv (foldHz (pf (5), 900, 2400), .25, .05, nowS + .02 * sr); }
        } else if (sp == SP_PHOENIX) {
            static const double DUR[4] = { 24, 9, 6, 10 }; auto& Ph = W.ph; Ph.t += dt;
            if (Ph.t >= DUR[Ph.s]) { Ph.s = (Ph.s + 1) % 4; Ph.t = 0; if (Ph.s == 0) { W.reborn++; W.keyMul = std::pow (2.0, (double) ((7 * W.reborn) % 12) / 12); flourish(); } }
            const double u = Ph.t / DUR[Ph.s];
            if (Ph.s == 1) { rm = 1 + .4 * u; am = 1.1 - .5 * u; cr = .9 * u; det = .05 * u; }
            else if (Ph.s == 2) { op = false; cr = 1; }
            else if (Ph.s == 3) { rm = .35; am = .45 + .3 * u; cr = .6 * (1 - u); Ph.beat -= dt; if (Ph.beat <= 0) { Ph.beat = 1.2 - .6 * u; toneEnv (std::max (40.0, ROOT / 4), .5, .3 * (.5 + u), nowS + .02 * sr); } }
            const double cI = Ph.s == 1 ? u : Ph.s == 2 ? .5 * (1 - u) : 0;
            if (cI > .02 && rnd() < dt * 30 * cI) nburst (Biquad::BP, 1500 + rnd() * 3500, 1.2, .012 + rnd() * .02, .1 + .1 * rnd(), nowS + .02 * sr, rnd() * 1.6 - .8);
        }
        if (SEA.id == SE_BLOOM) am *= 1 + .17 * BL.L;
        W.rateMul = rm; W.ampMul = am; W.open = op; W.det = det; W.crush = cr; W.muffle = mu; W.ringMul = rg;
    }
    void wildBreath() {
        if (! hasCR) return; const int sp = CR.special; const double t = nowS + .03 * sr;
        if (sp == SP_BUZZ) toneEnv (foldHz ((CR.humF > 0 ? CR.humF : 220) * 2.3, 200, 700), .3, .07, t);
        else if (sp == SP_THUNDER) {
            auto& T = WILD.tb; T.d = clampd (T.d + (rnd() - .5) * .9, .3, 3.2); const double lag = T.d * 2.92, g = .6 / (.6 + T.d);
            ENV.flash = 1; nburst (Biquad::HP, 2500, .7, .12, .2, t); nburst (Biquad::LP, 90 + 220 / (1 + T.d), .7, 2.8, .7 * g, t + lag * sr); toneEnv (48, 1.8, .3 * g, t + lag * sr); T.last = lag;
        }
    }
    void pollenDeposit (double t) {
        BL.L = 0; BL.dep++; BL.vis = std::min (1.0, BL.vis + .2); BL.shim = 2.2;
        for (int k = 0; k < 4; ++k) pushStrike ({ t + k * .06 * sr, k % 2, foldHz (pf (k * 2 + 3), 220, 1100), .4 });
        for (int j = 0; j < 7; ++j) nburst (Biquad::HP, 6000 + rnd() * 4000, .8, .05 + rnd() * .05, .05, t + j * .03 * sr, rnd() * 1.6 - .8);
        if (hasCR && CR.special == SP_BUZZ) toneEnv (foldHz ((CR.humF > 0 ? CR.humF : 220) * 2.3, 200, 700), .5, .09, t);
    }
    void wildBlip (const Wing&, double t, double bf, double vel) {
        if (! hasCR) return; const double pan = rnd() * 1.6 - .8;
        if (CR.special == SP_SCALES && rnd() < .7) nburst (Biquad::HP, 6500 + rnd() * 3000, .8, .05, .045 * vel, t, pan);
        if (SEA.id == SE_BLOOM) {
            const double pr = BL.F * S.slevel * (1 - .8 * ENV.rain) * (1 - .6 * ENV.tx);
            if (rnd() < .8 * pr) { nburst (Biquad::HP, 6000 + rnd() * 5000, .9, .03 + rnd() * .04, .04 * (.5 + BL.L), t, pan * (1 + std::abs (ENV.wind))); if (rnd() < .5) toneEnv (foldHz (bf * 4, 2000, 5200), .1, .02 * (.5 + BL.F), t, pan); }
            BL.L = std::min (1.0, BL.L + .015 * (.3 + BL.F) * (1 - ENV.tx)); if (BL.L >= 1) pollenDeposit (t);
        }
    }
    void bloomStep (double dt) {
        if (SEA.id != SE_BLOOM) return;
        const double cold = std::max (0.0, -ENV.temp); BL.open = clampd (1 - .8 * ENV.rain - .5 * cold - .4 * ENV.smoke - .5 * ENV.oil - .6 * ENV.tx, 0, 1);
        BL.vis = std::max (0.0, BL.vis - dt * .02); BL.F += (BL.open * (.2 + .8 * BL.vis) - BL.F) * (1 - std::exp (-dt / 8));
        if (ENV.rain > .2) BL.L = std::max (0.0, BL.L - dt * .05 * ENV.rain); BL.shim = std::max (0.0, BL.shim - dt);
    }

    // ---------------------------------------------------------------- creature and behavior application
    void applyCreature (int idx) {
        wildReset(); CR = derive (idx, S.sizefx); hasCR = true; QUIET = CR.special == SP_SILENT ? .28 : 1; SCALE = CR.sc; ROOT = CR.root; buildFlange();
        P.bmul = CR.mul == 1 ? 0 : CR.mul == 1.5 ? 1 : CR.mul == 2 ? 2 : 3; P.split = CR.split; P.ring = CR.ring; P.vlevel = CR.vlevel; P.dmode = CR.dub; P.room = CR.room;
        P.dub = CR.dubMix; P.dfb = CR.dfb; P.echo = CR.echo; P.air = CR.air; KMUL = CR.kickAmp; KDIV = CR.kickDiv; P.rate = 3.2 * CR.rateMul;
        FV[0].p.pan = -CR.pan; FV[1].p.pan = CR.pan; RV->p.size = CR.rvSize; RV->p.algo = CR.algo; FV[2].p.f0 = CR.humF > 0 ? CR.humF : 220; FV[2].f0S = FV[2].p.f0;
        setKey (SEA.keyMul); applyMode (mode); curCreature = idx; curSize = S.sizefx;
    }
    int curCreature = -1; double curSize = -1;
    void applyMode (int m) {
        const Mode& M = MODE (m); mode = m; modeT = 0; nextChange = 0; threatT = 0; tidalOpen = true;
        P.rate = M.rate; P.gamma = M.gamma; P.coup = M.coup; P.detune = M.detune; P.lag = M.lag; P.amp = M.amp; P.jitter = M.jitter; P.burst = M.burst; P.gate = M.gate;
        wings[0].ampMul = M.Lamp; wings[0].lagMul = M.Llag; wings[1].ampMul = 1; wings[1].lagMul = 1;
        if (hasCR) { P.restGlide = CR.restGlide; P.rate = (m == 0) ? 3.2 * CR.rateMul : P.rate; if (m == 0) { P.gamma = CR.gamma; P.gate = CR.gate; P.burst = CR.burst; } }
        Eg = .2; gateOpen = true;
    }
    void updateMode (double dt) {
        modeT += dt;
        if (mode == 1) {
            const double T = 7, u = std::fmod (modeT, T) / T; P.rate = 2.4 + 4.4 * u * u; P.lag = 1 - .5 * u;
            if (modeT > T && std::fmod (modeT - dt, T) > std::fmod (modeT, T)) { const double Ts = evTime(); sndKick (wings[0], 1, true, Ts); sndKick (wings[1], 1, true, Ts); sndClap (Ts); DB->throwNow (5); }
        } else if (mode == 2) {
            if (modeT > nextChange) { nextChange = modeT + .35 + rnd() * .6; static const double G[4] = { 0, PI / 2, PI, 2.4 }; P.gamma = G[(int) (rnd() * 4)]; P.rate = 4.2 + rnd() * 2.2; }
        } else if (mode == 3) {
            threatT += dt; const bool th = std::fmod (threatT, 4.5) > 3.6; P.jitter = th ? .2 : .05; P.amp = th ? 1.25 : 1;
        } else if (mode == 4) tidalOpen = std::fmod (modeT, 4) < 2.6;
    }

    // ---------------------------------------------------------------- the simulation
    bool dubReanchor = true;
    void fire (Wing& w, int k, const Joint& J) {
        const double vel = clampd (.35 + .8 * w.U, .25, 1), T = evTime();
        if (k == 0) {
            sndKick (w, vel, accent && w.side < 0, T);
            if (w.side < 0) accent = false;
            lift (.4 + .6 * clampd (J.A, 0, 1.2));
        } else if (k == 1) sndTick (w, vel, T);
        else {
            const double yn = clampd (J.tip / std::max (.25, w.ampMul * P.amp), -1, 1);
            const int sl = (int) SCALE->size();
            const int idx = (int) std::lround (3 * std::sin (breath + w.side * .7)) + (yn > 0 ? 3 : 0) + 3 + (rnd() < (P.jitter + ENV.jit) * 2 ? 1 : 0);
            double bf = pf (idx + (w.side < 0 ? 0 : sl));
            if (CR.special == SP_ALT) bf = pf (idx + (w.side < 0 ? 2 : -1));
            if (CR.special == SP_ECHO) DB->throwNow (5);
            if (CR.special == SP_TWOSCALE && CR.scB) bf = pfS (idx, w.side < 0 ? *CR.sc : *CR.scB);
            wildBlip (w, T, bf, vel);
            const int vi = w.side < 0 ? 0 : 1; while (bf > 1100) bf /= 2; while (bf < 150) bf *= 2;
            pushStrike ({ T, vi, bf, .5 * vel });
            if (SEA.id == SE_MIGRATE) for (int fk = 1; fk <= 4; ++fk) pushStrike ({ T + fk * .25 * bd() * sr, 2 + fk, bf, .5 * vel * std::pow (.65, fk) });
        }
    }
    void step (double dt) {
        updateMode (dt); seasonStep (dt); envStep (dt); wildStep (dt);
        Wing& L = wings[0]; Wing& R = wings[1];
        const double rt = RATE(), dtn = P.detune + ENVDET(), wl = TAU * rt * (1 - dtn / 2), wr = TAU * rt * (1 + dtn / 2), K0 = 14 * P.coup * S.coupling;
        double pll = 0;
        if (hostPlaying && S.block > 0) { const double posB = beatPos + laNow() / bd(), ref = TAU * MULS[(int) P.bmul] * posB + PI / 2; pll = S.block * LOCKM() * 12 * wrapPi (ref - L.th); }
        L.th += (wl + pll + K0 * std::sin (R.th - L.th - P.gamma)) * dt; R.th += (wr + K0 * std::sin (L.th - R.th + P.gamma)) * dt;
        breath += TAU * rt / P.meter * dt; const long bc = (long) std::floor (breath / TAU);
        if (bc > breathCnt) {
            breathCnt = bc; accent = true; wildBreath();
            if (CR.special == SP_BOOM) pushStrike ({ evTime(), 0, std::max (55.0, CR.root / 2), .9 });
            ST->p.lenSec = clampd (P.meter / rt, .1, 8); grabFlag = true;
        }
        const long cyc = (long) std::floor (L.th / TAU); if (! lastCycInit) { lastCycL = cyc; lastCycInit = true; }
        if (cyc > lastCycL) { lastCycL = cyc; if (gateOpen) Eg += 1; }
        if (P.gate) { const double d = rt * (1 - 2.2 / P.burst); Eg -= d * dt; if (gateOpen && Eg >= 2.4) gateOpen = false; else if (! gateOpen && Eg <= .2) gateOpen = true; } else gateOpen = true;
        const bool open = gateOpen && tidalOpen && ! SEA.rest && ENV.silence <= 0 && WILD.open && WILD.sil <= 0;
        for (int wi = 0; wi < 2; ++wi) {
            Wing& w = wings[wi]; const double tgt = open ? 1 : 0, k = tgt > w.env ? 14 : 18;
            w.env += (tgt - w.env) * std::min (1.0, dt * k);
            const Joint J = joint (w); const double sp = std::abs (J.tip - w.yPrev) / dt / (TAU * 4); w.yPrev = J.tip;
            w.U += (sp - w.U) * std::min (1.0, dt * 30); if (! w.sing && w.U > .5) w.sing = true; if (w.sing && w.U < .35) w.sing = false;
            if (! w.cntInit) { for (int i = 0; i < 4; ++i) w.cnt[i] = (long) std::floor ((w.th - EVD (i).ph) / TAU); w.cntInit = true; }
            for (int i = 0; i < 4; ++i) {
                const long nn = (long) std::floor ((w.th - EVD (i).ph - w.jit[i]) / TAU);
                if (nn > w.cnt[i]) { w.cnt[i] = nn; w.jit[i] = (rnd() * 2 - 1) * (P.jitter + ENV.jit) * 2; if (J.A > .12) fire (w, EVD (i).k == 0 ? 0 : EVD (i).k == 1 ? 1 : 2, J); }
            }
        }
        if (std::abs (wrapPi (L.th - R.th)) < .45 && (long) std::floor (L.th / TAU) > L.clapN) { L.clapN = (long) std::floor (L.th / TAU); if (wings[0].env * P.amp > .5) { sndClap (evTime()); DB->throwNow (5); } }
        stepFeathers (dt);
        // host-grid events: a turbine blade every two beats, a light circle on the eighths
        const long slot = (long) std::floor (beatPos / .25);
        if (slot != lastSlot) { lastSlot = slot; const double ts = nowS; if (ENV.tb > .2 && slot % 8 == 0) whoosh (ts); if (ENV.gl > .25 && slot % 2 == 0) circleLight (ts); }
    }
    long lastSlot = -1000000; bool grabFlag = false;

    // ---------------------------------------------------------------- per-sub-block audio parameter updates (updateAudio in the web version)
    void updateAudio() {
        for (int i = 0; i < 7; ++i) {
            auto& v = FV[i];
            if (i == 2) { const double ea = (wings[0].env + wings[1].env) / 2; v.p.u = (CR.hum) ? .3 + 1.05 * ea : 0; v.p.f0 = CR.humF > 0 ? CR.humF * SEA.keyMul * KW() : 220; v.p.split = std::min (.14, P.split); v.p.zeta = .004; v.p.level = .2 * (P.vlevel * ENV.lvl); v.p.amp = .5; continue; }
            if (i >= 3) {
                v.p.split = P.split; v.p.zeta = zetaR(); v.p.amp = .5;
                if (SEA.id == SE_MIGRATE) { static const double PAN[4] = { -.45, .45, -.85, .85 }; v.p.u = 0; v.p.level = .4 * (P.vlevel * ENV.lvl) * S.slevel * 1.4; v.p.pan = PAN[i - 3]; }
                else if (SEA.id == SE_MATING && i <= 4) { v.p.u = SEA.mu; v.p.f0 = i == 3 ? SEA.fM : SEA.fF; v.p.level = .3 * S.slevel * 1.4; v.p.pan = i == 3 ? -.45 : .45; }
                else { v.p.u = 0; v.p.level = 0; }
                continue;
            }
            const Wing& w = wings[i]; const double uu = w.sing ? 1.05 + 1.5 * clampd ((w.U - .35) / .5, 0, 1) : clampd (w.U / .5, 0, .95);
            v.p.u = uu * clampd (w.env * 1.2, 0, 1); v.p.split = P.split; v.p.zeta = zetaR(); v.p.level = .5 * (P.vlevel * ENV.lvl); v.p.amp = .5;
        }
        seasonAudio();
        // effective FX values: the creature's own, scaled by the host trims, then pulled back by Tame
        const double tm = S.tame, c = modMix(.5, modH);
        const double dubE = clampd (P.dub * std::pow (2.0, S.dubTrim), 0, 1), dfbE = clampd (P.dfb * std::pow (2.0, S.dfbTrim), 0, 1.2), echoE = clampd (P.echo * std::pow (2.0, S.echoTrim), 0, 1), airE = clampd (P.air * std::pow (2.0, S.airTrim), 0, 1), roomE = clampd (P.room * std::pow (2.0, S.roomTrim), 0, 1);
        merge = S.merge; flangeAmt = S.flange; bpFreq1 = 440 * (1 + .125 * c); bpFreq2 = 660 * (1 - .17 * c);
        const int fidx = (int) clampd (std::floor (modMix(.5, modH) * (double) FLANGE.size()), 0, (double) FLANGE.size() - 1); dlyTarget = 1 / FLANGE[(size_t) fidx];
        fbGain = .2 + .55 * S.flange;
        const double swm = modMix (.5, .3 + .7 * modSway); auto& xp = XS->p;
        xp.timeL = clampd (.5 * bd(), .1, 1.4); xp.timeR = clampd (1.0 * bd(), .1, 2.4);
        xp.fb = echoE * (.35 + .5 * swm) * (1 - .55 * tm); xp.cross = .25 + .4 * swm; xp.d2r = .15 + .6 * airE; xp.r2d = .1 + .2 * airE; xp.decay = .35 + .6 * airE; xp.size = .4 + .5 * airE;
        xp.damp = clampd (.6 - .4 * modMix (.5, modH), .1, .9); xp.mix = std::min (.9, std::max (echoE, airE) * .8) * (1 - .3 * tm);
        auto& dp = DB->p; dp.time = (CR.special == SP_ECHO ? .25 : .75) * bd(); dp.t0f = nowS - beatPos * spb();
        dp.mode = P.dmode; dp.fb = dfbE * (1 - .55 * tm); dp.mix = dubE * (1 - .3 * tm);
        const double u = mode == 1 ? std::fmod (modeT, 7) / 7 : 0;
        dp.melt = (mode == 1 ? .65 * u * u : 0) * (1 - .6 * tm); dp.insanity = (mode == 2 ? .55 : 0) * (1 - .6 * tm); dp.warp = (mode == 3 ? .45 : 0) * (1 - .6 * tm); dp.wow = mode == 4 ? .45 : .2; dp.spread = .3 + .5 * modMix (.5, modSway);
        RV->p.decay = CR.decay * (.6 + .8 * modMix (.5, modSettle)) * ENV.decayMul; RV->p.damp = clampd (.6 - .4 * modMix (.5, modH) + ENV.damp, .1, .95);
        rvMix = roomE * (1 - .25 * tm);
        ST->p.speed = S.sspeed; ST->p.mode = S.smode; stretchAmt = S.stretch;
    }
    double merge = .45, flangeAmt = .4, bpFreq1 = 440, bpFreq2 = 660, dlyTarget = 1 / 440., fbGain = .4, rvMix = .15, stretchAmt = .35;
    // chain state
    Biquad bpA[2], bpB[2], lpF[2], envLP[2]; std::vector<float> dlyBuf[2]; size_t dlyW[2] = { 0, 0 }; double dlySmooth = 1 / 440., lpFeed[2] = { 0, 0 };
    double stAmt = 0, lpzL = 0, lpzR = 0, tEnv = 0, rEnv = 0, gSm = 1, rvMixS = 0, padG = 0; int stPhase = 0, stCount = 0; double padPh[2] = {};
    Comp busComp, mastComp; double envLPf = 18000;

    // ---------------------------------------------------------------- one sub-block of audio
    void sub (int n, float* outL, float* outR) {
        const double dt = (double) n / sr;
        sync2();
        step (dt); updateAudio();
        double sL[SUB] = {}, sR[SUB] = {}, eL[SUB] = {}, eR[SUB] = {};
        // feather voices and swarm, with strikes landing on their sample
        int pos = 0; const double blockEnd = nowS + n;
        while (! strikeQ.empty() && strikeQ.front().t < blockEnd) {
            const Strike e = strikeQ.front(); strikeQ.pop_front(); const int k = (int) clampd (std::llround (e.t - nowS), 0, n);
            if (k > pos) { for (auto& v : FV) v.process (sL + pos, sR + pos, k - pos); pos = k; }
            FV[e.v].p.f0 = e.f0; FV[e.v].strike (e.vel);
        }
        if (n > pos) for (auto& v : FV) v.process (sL + pos, sR + pos, n - pos);
        SWM.process (sL, sR, n);
        // pad while gliding
        padG += (((P.restGlide && ! gateOpen && P.gate) || SEA.rest ? .05 : 0) - padG) * (1 - std::exp (-dt / .08));
        if (padG > 1e-5) for (int i = 0; i < n; ++i) { padPh[0] += TAU * 220 / sr; padPh[1] += TAU * 330 / sr; const double y = (std::sin (padPh[0]) + std::sin (padPh[1])) * padG; sL[i] += y; sR[i] += y; }
        renderVox (n, sL, sR, eL, eR);
        for (int i = 0; i < n; ++i) { sL[i] *= .9; sR[i] *= .9; }
        // stage 1: merge (two bands closing toward each other), stage 2: in-key flange
        for (int c = 0; c < 2; ++c) { bpA[c].set (Biquad::BP, bpFreq1, 10, sr); bpB[c].set (Biquad::BP, bpFreq2, 10, sr); }
        double* ch[2] = { sL, sR };
        const double wet1 = 1.8 * merge, dry1 = 1 - .55 * merge, wetF = .9 * flangeAmt, dryF = 1 - .3 * flangeAmt, ds = std::exp (-dt / .06);
        (void) ds;
        for (int i = 0; i < n; ++i) {
            dlySmooth += (dlyTarget - dlySmooth) * (1 - std::exp (-1 / (sr * .06)));
            const double dsmp = std::max (dlySmooth * sr, 128.0); // a feedback loop in Web Audio cannot be shorter than one 128-sample quantum; kept for the same sound
            for (int c = 0; c < 2; ++c) {
                const double x = ch[c][i];
                const double merged = x * dry1 + (bpA[c].process (x) + bpB[c].process (x)) * wet1;
                auto& buf = dlyBuf[c]; const size_t N = buf.size(); double rp = (double) dlyW[c] - dsmp; while (rp < 0) rp += (double) N; const size_t r0 = (size_t) rp % N, r1 = (r0 + 1) % N; const double fr = rp - std::floor (rp);
                const double dout = buf[r0] * (1 - fr) + buf[r1] * fr;
                buf[dlyW[c]] = (float) (merged + fbGain * lpF[c].process (dout)); dlyW[c] = (dlyW[c] + 1) % N;
                ch[c][i] = merged * dryF + dout * wetF;
            }
        }
        // stage 3+: Stretch (send), Dub, crush, XSpace, reverb, Tame
        fxChain (n, sL, sR);
        // bus compressor, environment low-pass, then the environment bus joins
        busComp.process (sL, sR, n);
        renderBeds (n, eL, eR);
        const double fc = 18000 * std::pow (.08, std::max (ENV.muffle, WILD.muffle)); envLPf += (fc - envLPf) * .2;
        for (int c = 0; c < 2; ++c) envLP[c].set (Biquad::LP, envLPf, .5, sr);
        double rL[SUB], rR[SUB]; { double pkE = 0; for (int i = 0; i < n; ++i) pkE = std::max (pkE, std::max (std::abs (eL[i]), std::abs (eR[i]))); if (pkE > 1e-5) envHold = (int) (3 * sr); else envHold = std::max (0, envHold - n); }
        for (int i = 0; i < n; ++i) {
            sL[i] = envLP[0].process (sL[i]); sR[i] = envLP[1].process (sR[i]);
            eL[i] = elp[0].process (eL[i]); eR[i] = elp[1].process (eR[i]);
            double wl = 0, wr = 0; if (envHold > 0) RVE->process (eL[i], eR[i], wl, wr); rL[i] = wl * .7; rR[i] = wr * .7;
            sL[i] += eL[i] + rL[i]; sR[i] += eR[i] + rR[i]; sL[i] *= .8; sR[i] *= .8;
        }
        mastComp.process (sL, sR, n);
        for (int i = 0; i < n; ++i) { const double g = S.outGain; outL[i] = (float) clampd (sL[i] * g, -1.5, 1.5); outR[i] = (float) clampd (sR[i] * g, -1.5, 1.5); }
        nowS += n;
    }
    void fxChain (int n, double* L, double* R) {
        double dL[SUB] = {}, dR[SUB] = {}; float fL[SUB] = {}, fR[SUB] = {}, gL[SUB] = {}, gR[SUB] = {};
        for (int i = 0; i < n; ++i) { dL[i] = L[i]; dR[i] = R[i]; }
        // Stretch runs as a send with soft fades (the same glue as the web version)
        const bool wantGrab = grabFlag && stPhase == 0; if (wantGrab) { stPhase = 1; stCount = 0; grabFlag = false; }
        const double spd0 = ST->p.speed * (ST->p.mode == 0 ? std::pow (2.0, ST->p.pitch / 12) : 1); int seg0 = ST->segLen; double fp0 = ST->pos; const int mode0 = ST->p.mode;
        int grabAt = -1;
        if (stPhase == 2) { grabAt = 0; stPhase = 0; stCount = 0; stFadeIn = 256; seg0 = std::min ((int) (ST->p.lenSec * sr), ST->N - 16); fp0 = 0; }
        ST->process (L, R, n, grabAt);
        stAmt += (stretchAmt * .8 - stAmt) * .2;
        if (ST->segLen > 0) {
            const double edge = .006 * sr * std::max (.25, std::abs (spd0)); const bool tapeLike = (mode0 == 0 || mode0 == 4);
            for (int i = 0; i < n; ++i) {
                double gw = 1;
                if (tapeLike && seg0 > 0) { double pp = std::fmod (fp0 + spd0 * (i + 1), (double) seg0); const double ed = std::min (pp, seg0 - pp) / edge; gw = ed >= 1 ? 1 : (ed <= 0 ? 0 : ed * ed * (3 - 2 * ed)); }
                if (stPhase == 1) { gw *= std::max (0.0, 1 - (double) (stCount + i) / 256); }
                if (stFadeIn > 0) { gw *= 1 - (double) stFadeIn / 256; --stFadeIn; }
                lpzL += (L[i] * gw - lpzL) * .35; lpzR += (R[i] * gw - lpzR) * .35;
                L[i] = dL[i] + stAmt * lpzL; R[i] = dR[i] + stAmt * lpzR;
            }
            if (stPhase == 1) { stCount += n; if (stCount >= 256) stPhase = 2; }
        } else { for (int i = 0; i < n; ++i) { L[i] = dL[i]; R[i] = dR[i]; } if (stPhase == 1) stPhase = 2; }
        // Tame: remember the dry-before-effects signal
        const double tame = S.tame; double tmL[SUB], tmR[SUB]; for (int i = 0; i < n; ++i) { tmL[i] = L[i]; tmR[i] = R[i]; fL[i] = (float) L[i]; fR[i] = (float) R[i]; }
        DB->frame = nowS; DB->process (fL, fR, gL, gR, n); CKR->process (gL, gR, fL, fR, n);
        for (int i = 0; i < n; ++i) { L[i] = fL[i]; R[i] = fR[i]; }
        XS->process (L, R, n); RV->update();
        for (int i = 0; i < n; ++i) {
            const double xL = L[i], xR = R[i]; double wl, wr; RV->process (xL, xR, wl, wr); rvMixS += (rvMix - rvMixS) * .002; L[i] = xL + wl * rvMixS; R[i] = xR + wr * rvMixS;
        }
        if (tame > 0) {
            const double td = tame * (1 - CKR->p.mix), dep = .6 * td, kc = 1.6 * td;
            for (int i = 0; i < n; ++i) {
                const double xin = .5 * (std::abs (tmL[i]) + std::abs (tmR[i])); tEnv += (xin - tEnv) * (xin > tEnv ? .01 : .0004);
                const double rl = L[i] - tmL[i], rr = R[i] - tmR[i], ra = std::max (std::abs (rl), std::abs (rr)); rEnv += (ra - rEnv) * (ra > rEnv ? .004 : .0003);
                gSm += ((1 - dep * tEnv / (tEnv + .05)) / (1 + kc * rEnv / (rEnv + .12) * 2) - gSm) * .005;
                L[i] = tmL[i] + rl * gSm; R[i] = tmR[i] + rr * gSm;
            }
        }
        for (int i = 0; i < n; ++i) { // soft knee above 0.8 so stacked effects cannot clip hard
            const double a0 = L[i], a1 = R[i];
            if (std::abs (a0) > .8) L[i] = (a0 < 0 ? -1 : 1) * (.8 + .2 * std::tanh ((std::abs (a0) - .8) / .2));
            if (std::abs (a1) > .8) R[i] = (a1 < 0 ? -1 : 1) * (.8 + .2 * std::tanh ((std::abs (a1) - .8) / .2));
        }
    }
    int stFadeIn = 0, envHold = 0;

    // ---------------------------------------------------------------- host parameter sync
    void sync (const Settings& s, const HostInfo& h) {
        bool resize = std::abs (s.sizefx - S.sizefx) > 1e-6; S = s;
        bpm = h.bpm > 20 ? h.bpm : bpm; hostPlaying = h.playing && h.hasPpq; if (hostPlaying) hostPpq = h.ppq;
        if (! hostPlaying && h.hasPpq == false) {} // free-running beat clock
        if (curCreature != S.creature || resize) applyCreature (S.creature);
    }
    int lastMode = -1, lastSeason = -1, lastWeather = -1; bool lastEv[9] = {};
    void sync2() { // changes of mode, season, weather and events (checked every sub-block, cheap)
        if (S.mode != lastMode) { lastMode = S.mode; if (ENV.e.forced) ENV.e.prevMode = S.mode; else applyMode (S.mode); }
        const int es = (hasCR && CR.special == SP_THIRTY && S.season == 0) ? (int) SE_SWARM : S.season; // the Simurgh is its own swarm
        if (es != lastSeason) { lastSeason = es; setSeason (es); }
        if (S.weather != lastWeather) { lastWeather = S.weather; setWeather (S.weather); }
        for (int i = 0; i < 9; ++i) if (S.events[i] != lastEv[i]) { lastEv[i] = S.events[i]; setEvent (i, S.events[i]); }
    }

    // ---------------------------------------------------------------- status text for the editor
    char statusBuf[2][320] = {}; std::atomic<int> statusIdx { 0 }; int statusCnt = 0;
    void writeStatus() {
        if (! hasCR) return; char b[320]; const int w = 1 - statusIdx.load();
        const char* seas[] = { "", "swarm", "migrate", "pillage", "mating", "bloom" };
        std::snprintf (b, sizeof b, "%s · %s · %.0f BPM%s | root %.0f Hz | %s", CR.c->name, modeName (mode), bpm, hostPlaying ? "" : " (free)", ROOT, SEA.id ? seas[SEA.id] : "no season");
        std::strncpy (statusBuf[w], b, sizeof statusBuf[w] - 1);
        char* t = statusBuf[w]; size_t l = std::strlen (t);
        if (SEA.id == SE_SWARM || SEA.id == SE_PILLAGE) std::snprintf (t + l, sizeof statusBuf[w] - l, " · %d voices, fusion %d%%", (int) SWM.p.n, (int) std::lround (SWM.r * 100));
        else if (SEA.id == SE_BLOOM) std::snprintf (t + l, sizeof statusBuf[w] - l, " · flowers %d%%, pollen %d%%, delivered %d", (int) std::lround (BL.F * 100), (int) std::lround (BL.L * 100), BL.dep);
        else if (CR.special == SP_WARM) std::snprintf (t + l, sizeof statusBuf[w] - l, " · thorax %d C", (int) std::lround (15 + 25 * WILD.warm));
        else if (CR.special == SP_PHOENIX) { static const char* PN[] = { "flight", "burn", "ash", "egg" }; std::snprintf (t + l, sizeof statusBuf[w] - l, " · %s, rebirths %d", PN[WILD.ph.s], WILD.reborn); }
        else if (CR.special == SP_THUNDER) std::snprintf (t + l, sizeof statusBuf[w] - l, " · storm %.1f km, thunder lags %.1f s", WILD.tb.d, WILD.tb.d * 2.92);
        else if (CR.special == SP_FLASH) std::snprintf (t + l, sizeof statusBuf[w] - l, " · %s", WILD.fy.burst ? "flashing" : "dark");
        statusIdx.store (w);
    }
};

} // namespace wm
