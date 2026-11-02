// AKME D+0 MUSIC · Plunger Model — seeded dub echo (C++ port of DubCore)
// Feedback can run past unity; a governor holds the level. Lock moves repeats from free-running onto the host grid.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "Rng.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846 // MSVC doesn't define it by default
#endif

namespace pm {

enum DubThrow { THR_ALWAYS = 0, THR_POPS, THR_PLUNGER, THR_KEYS, THR_SEED, THR_BUTTON };

class DubCore {
public:
    struct Params { double time = .375, fb = .4, mix = .3, tone = 5000, swing = .25, groove = .3, ride = .5, spread = .5, wow = .2, flutter = .15; int seed = 7; int thr = THR_ALWAYS; double lock = 1, t0f = 0;
                    // Echo+ : mode and in-loop processing (each repeat goes through these again, so they compound)
                    int mode = 0, heads = 6; double fres = .5, flfo = .3, fdepth = .4, modDepth = .5, pitch = 0, shimmer = 0, diffuse = 0, drive = 0, crush = 0, loCut = 120, duck = 0, insanity = 0, reverse = 0, warp = 0, melt = 0; bool freeze = false; } p;
    enum { M_DUB = 0, M_TAPE, M_PINGPONG, M_REVERSE, M_MULTITAP, M_BBD, M_RESONATOR, M_SPECTRAL, M_GRANULAR, M_SPACE, M_DYNAMIC, M_FILTER, M_MOD };
    bool idle() const { return p.mix <= 0 && mixS < 1e-5 && ! p.freeze; }
    double frame = 0; // absolute sample position of the block start on the grid clock

    explicit DubCore (double sampleRate) : sr (sampleRate) {
        iL.assign (N, 0.f); iR.assign (N, 0.f); bL.assign (N, 0.f); bR.assign (N, 0.f);
        const double d = .375 * sr; d1A = d1B = d2A = d2B = d; genPat();
        psL.assign (PSN, 0.f); psR.assign (PSN, 0.f);
        static const double AP[4] = { .0071, .0113, .0167, .0229 }; for (int j = 0; j < 4; ++j) { apN[j] = (int) (AP[j] * sr); apL[j].assign ((size_t) apN[j], 0.f); apR[j].assign ((size_t) apN[j] + 37, 0.f); }
        psWin = (int) (.06 * sr);
    }
    void setSeed (int s) { p.seed = s; genPat(); k = 0; }
    void clear() { std::fill (iL.begin(), iL.end(), 0.f); std::fill (iR.begin(), iR.end(), 0.f); std::fill (bL.begin(), bL.end(), 0.f); std::fill (bR.begin(), bR.end(), 0.f);
                   std::fill (psL.begin(), psL.end(), 0.f); std::fill (psR.begin(), psR.end(), 0.f); for (auto& a2 : apL) std::fill (a2.begin(), a2.end(), 0.f); for (auto& a2 : apR) std::fill (a2.begin(), a2.end(), 0.f);
                   lpL = lpR = hpL = hpR = env = 0; wsS = wsT = panS = panT = meltPitch = 0; meltRev = meltHold = false; tpL = tpR = lcHpL = lcHpR = 0; thrSteps = 0; fph = 0; dynEnv = 0; for (int k = 0; k < 4; ++k) ldL[k] = ldR[k] = 0; sz1L = sz2L = sz1R = sz2R = 0; bbdL = bbdR = bbdHL = bbdHR = 0; }
    // src: what fired it (THR_* index) — kept with a counter so the editor can show each throw and its cause
    void calm() { fbS = std::min (fbS, .2); fbT = std::min (fbT, .2); gov = std::min (gov, .5); }
    void throwNow (int src = THR_BUTTON) { thrSteps = 2; thrG = 1; fbT = std::min (1.35, fbT + .2); ++throws; lastSrc = src; }
    // read by the editor's throw view
    uint32_t throws = 0; int lastSrc = -1, stepNow = 0, lastIns = 0; float wetPk = 0, inPk = 0; uint16_t seedMask() const { uint16_t m = 0; for (int j = 0; j < 16; ++j) if (pat[j].thr < .22) m |= (uint16_t) (1u << j); return m; }
    double sendLevel() const { return sendS; } double fbLevel() const { return fbS; } double mixLevel() const { return mixS; }

    double extBend = 0, extTone = 1; // the plunger playing the loop: pressure bends the repeats (delay-length factor), the cup's air sets their tone
    void process (const float* inL, const float* inR, float* oL, float* oR, int n, const float* sendL = nullptr, const float* sendR = nullptr) {
        const double ka = 1 - std::exp (-1 / (sr * .005)), kr = 1 - std::exp (-1 / (sr * .3)), hpA = std::exp (-2 * M_PI * 120 / sr);
        lcA = std::exp (-2 * M_PI * std::max (20., p.loCut) / sr); tapeLc = 1 - std::exp (-2 * M_PI * 7000 / sr);
        const double wowAmt = p.wow + (p.mode == M_TAPE ? .35 : 0) + (p.mode == M_SPACE ? .45 : 0) + (p.mode == M_BBD ? .1 : 0) + (p.mode == M_MOD ? 1.5 * p.modDepth : 0);
        bbdA = 1 - std::exp (-2 * M_PI * std::min (sr * .45, 2500 + 6000 * (.375 / std::max (.05, p.time)) * .5) / sr); grLen = std::max (480.0, std::min (sr * .12, sr * (.025 + .08 * (1 - p.groove))));
        const double wsG = 1 - std::exp (-1 / (sr * (.04 + .5 * (1 - p.warp)))); // warp glide: slower swoops at low Warp
        const double gc = .55 + 1.1 * p.melt, fbK = 1.1 + 1.6 * p.melt, foldA = std::max (0., (p.melt - .2) / .8), crushE = std::max (p.crush, p.melt > .5 ? (p.melt - .5) * 1.2 : 0.);
        for (int i = 0; i < n; ++i) {
            if (--stepC <= 0) {
                ++k; const double T = std::max (.02, p.time) * sr, lk = std::max (0., std::min (1., p.lock));
                const double sw = ((k & 1) ? 1 : -1) * p.swing * .33, dFree = T * (1 + sw) + pat[k & 15].jit * p.groove * .012 * sr, sFree = T * (1 + sw);
                const double F = frame + i; const long long g = (long long) std::llround ((F - p.t0f + T / 2) / T);
                const double og = off (g, T), op = off (g - 1, T);
                const double d1L = T + og, d2L = T + og - op, sL = p.t0f + (g + 1) * T + off (g + 1, T) - T / 2 - F;
                const int sidx = (int) ((((lk >= .5 ? g : (long long) k) % 16) + 16) % 16); const Pat& pt = pat[sidx];
                auto lim = [&] (double v) { return std::min ((double) N - 4096, std::max (p.mode == M_RESONATOR ? 8. : 64., v)); };
                d1A = d1A * (1 - xf) + d1B * xf; d2A = d2A * (1 - xf) + d2B * xf;
                d1B = lim (dFree * (1 - lk) + d1L * lk); d2B = lim (dFree * (1 - lk) + d2L * lk); xf = 0;
                stepC = std::max (32LL, std::llround (sFree * (1 - lk) + std::max (32., sL) * lk));
                stepNow = sidx; if (p.thr == THR_SEED && pt.thr < .22 && thrSteps == 0) { thrSteps = 1; ++throws; lastSrc = THR_SEED; }
                if (thrSteps > 0) { thrG = 1; --thrSteps; } else thrG = 0;
                fbT = p.fb * (1 + (pt.fb - 1) * p.ride) + (thrG ? .2 : 0); curSnd = 1 - p.ride * (1 - pt.snd); // the seed accents which steps feed the echo, in every Throw mode
                toneT = std::min (16000., std::max (200., p.tone * (1 + (pt.tone - 1) * p.ride)));
                // insanity: on some repeats something breaks — time jumps, pitch flips, reversals, feedback spikes, filter slams
                insEv = 0; lastIns = 0;
                if (p.insanity > 0 && R() < p.insanity * .6) {
                    const int e = (int) (R() * 5); insEv = e + 1; lastIns = e + 1;
                    if (e == 0) { const double j = R() < .5 ? .5 : (R() < .5 ? 1.5 : .25); d1B = lim (d1B * j); d2B = lim (d2B * j); }
                    else if (e == 3) fbT = std::min (1.4, fbT + .3 + .4 * p.insanity);
                    else if (e == 4) toneT = R() < .5 ? 350 : 14000;
                }
                // warp: the echo-time knob grabbed and twisted - the delay glides to a new length, so repeats swoop in pitch
                if (p.warp > 0 && R() < .25 + .6 * p.warp) wsT = (R() * 1.5 - .5) * p.warp * .6;
                else if (p.warp <= 0) wsT = 0;
                // meltdown: several breakages per repeat, compounding in the loop (pitch stacks, ratchets, reversals, freeze bursts, scatter)
                meltPitch = 0; meltRev = false; meltHold = false;
                if (p.melt > 0) { const int rolls = 1 + (int) (p.melt * 3);
                    for (int r = 0; r < rolls; ++r) if (R() < p.melt * .7) { const int e = (int) (R() * 7) % 7; lastIns = 1 + e % 5;
                        if (e == 0) { static const double J[5] = { .5, .25, .125, 1.5, 2 }; const double j = J[(int) (R() * 5) % 5]; d1B = lim (d1B * j); d2B = lim (d2B * j); }
                        else if (e == 1) { static const double PS[8] = { -12, -7, -5, 5, 7, 12, 19, 24 }; meltPitch += PS[(int) (R() * 8) % 8]; }
                        else if (e == 2) meltRev = true;
                        else if (e == 3) fbT = std::min (1.2 + .6 * p.melt, fbT + .3 + .5 * p.melt);
                        else if (e == 4) toneT = R() < .5 ? 250 + 400 * R() : 16000;
                        else if (e == 5) panT = (R() * 2 - 1) * p.melt;
                        else meltHold = true; }
                    meltPitch = std::max (-24., std::min (24., meltPitch)); }
                else panT = 0;
                revStart = w; revLen = std::max (256.0, std::min ((double) N / 4, T));
                if (p.mode == M_SPECTRAL) { // each repeat through a new band: walks up, or jumps when the seed says so
                    specIdx = (specIdx + (pt.jit > .3 ? 3 : 1)) % 10; const double fc = 220 * std::pow (2., specIdx * .55 + p.tone / 12000 * 1.5);
                    const double w0 = 2 * M_PI * std::min (fc, sr * .4) / sr, al = std::sin (w0) / (2 * 4.5), a0 = 1 + al;
                    sb0 = al / a0; sb2 = -al / a0; sa1 = -2 * std::cos (w0) / a0; sa2 = (1 - al) / a0; }
                tapStepT = T;
            }
            xf = std::min (1., xf + 1. / 480);
            fbS += (fbT - fbS) * .0008; toneS += (toneT - toneS) * .0008;
            const double send = p.thr == THR_ALWAYS ? curSnd : thrG; sendS += (send - sendS) * .004; mixS += (p.mix - mixS) * .002;
            wdr += (R() * 2 - 1 - wdr) * .0005; wph += 2 * M_PI * (.5 + .3 * wdr) / sr;
            // wow: slow capstan drift (~0.5 Hz). flutter: fast, uneven wobble (~6-9 Hz) with a little scrape; tape and space echo add some of their own
            flph += 2 * M_PI * (6.8 + 1.6 * wdr) / sr; if (flph > 2 * M_PI * 64) flph -= 2 * M_PI * 64;
            const double flAmt = p.flutter + (p.mode == M_TAPE ? .15 : 0) + (p.mode == M_SPACE ? .2 : 0);
            const double wo = (std::sin (wph) + (p.mode == M_MOD ? .5 * std::sin (wph * 2.3 + 1) : 0)) * wowAmt * .0025 * sr
                            + (.7 * std::sin (flph) + .3 * std::sin (flph * 2.37 + .5) + .15 * wdr) * flAmt * .00045 * sr;
            wsS += (wsT - wsS) * wsG; panS += (panT - panS) * .002;
            exS += (extBend - exS) * .0015; const double dMax = std::max (std::max (d1A, d1B), std::max (d2A, d2B)), wf = std::min ((1 + wsS) * (1 + exS), ((double) N - 4096) / std::max (1., dMax));
            const double a1 = w - (d1A * wf + wo), b1 = w - (d1B * wf + wo), a2 = w - (d2A * wf + wo), b2 = w - (d2B * wf + wo);
            double zL, zR;
            if (p.mode == M_GRANULAR) { // two overlapping grains, each reading from a random spot around the echo time
                for (int gI = 0; gI < 2; ++gI) { if (--grT[gI] <= 0) { grT[gI] = grLen; grOff[gI] = (R() - .5) * grLen * (2 + 6 * p.groove); grRt[gI] = R() < .25 ? (R() < .5 ? .5 : 2.) : 1; grP[gI] = 0; } }
                zL = zR = 0; for (int gI = 0; gI < 2; ++gI) { const double ph = 1 - (double) grT[gI] / grLen, win = std::sin (M_PI * ph); grP[gI] += grRt[gI] - 1;
                    const double pa = w - (d1B + wo) + grOff[gI] + grP[gI], pb = w - (d2B + wo) + grOff[gI] + grP[gI];
                    zL += (rd (iL, pa) + rd (bL, pb)) * win; zR += (rd (iR, pa) + rd (bR, pb)) * win; }
            } else if (p.mode == M_SPACE) { // three playback heads at 1/3, 2/3 and the full echo time; the head switch picks which ones play (and feed back)
                static const int HS[7] = { 1, 2, 4, 3, 6, 5, 7 }; const int hm = HS[std::max (0, std::min (6, p.heads))]; int nh = 0; zL = zR = 0;
                for (int h = 0; h < 3; ++h) if (hm & (1 << h)) { ++nh; const double fr2 = (h + 1) / 3.0, pa = w - ((d1A * (1 - xf) + d1B * xf) * fr2 + wo), pb = w - ((d2A * (1 - xf) + d2B * xf) * fr2 + wo);
                    const double ni = 1 / std::sqrt (3.0), nb = 1 / 3.0; zL += rd (iL, pa) * ni + rd (bL, pb) * nb; zR += rd (iR, pa) * ni + rd (bR, pb) * nb; }
                if (nh == 1) { zL *= 1.7; zR *= 1.7; } // one head: no loss from sharing the loop
            } else {
                zL = rd (iL, a1) * (1 - xf) + rd (iL, b1) * xf + rd (bL, a2) * (1 - xf) + rd (bL, b2) * xf;
                zR = rd (iR, a1) * (1 - xf) + rd (iR, b1) * xf + rd (bR, a2) * (1 - xf) + rd (bR, b2) * xf; }
            if (p.mode == M_MULTITAP) { // three extra taps inside the echo time, seeded levels and pans
                static const double TF[3] = { .25, .5, .75 };
                for (int j = 0; j < 3; ++j) { const double pos = w - (d1B * TF[j] + wo), gl = tapLv[j], pn = tapPan[j];
                    const double x = rd (iL, pos) * .5 + rd (iR, pos) * .5; zL += x * gl * (1 - pn); zR += x * gl * (1 + pn); } }
            // reverse: the repeat plays the last echo-length of the loop backwards
            const double rv = std::max (p.reverse, p.mode == M_REVERSE ? 1. : 0.) + (insEv == 3 ? 1 : 0) + (meltRev ? 1 : 0);
            if (rv > 0) {
                const double j = (double) ((w - revStart) & M), ph = std::fmod (j, revLen), pos = revStart - d1A - ph;
                const double win = std::min (1., std::min (ph, revLen - ph) / (sr * .004));
                const double rL = (rd (iL, pos) + rd (bL, pos)) * win, rR = (rd (iR, pos) + rd (bR, pos)) * win, m = std::min (1., rv);
                zL = zL * (1 - m) + rL * m; zR = zR * (1 - m) + rR * m;
            }
            if (--lcCount <= 0 || toneS != lcTone) { lcCount = 16; lcTone = toneS; lc = 1 - std::exp (-2 * M_PI * toneS / sr); }
            { const double lc2 = extTone < .999 ? lc * (.08 + .92 * extTone) : lc; lpL += lc2 * (zL - lpL); lpR += lc2 * (zR - lpR); }
            hpL = hpA * hpL + (1 - hpA) * lpL; hpR = hpA * hpR + (1 - hpA) * lpR;
            const double yL = lpL - hpL, yR = lpR - hpR;
            const double lv = std::max (std::abs (yL), std::abs (yR)); env += (lv > env ? ka : kr) * (lv - env);
            gov += ((env > gc ? gc / env : 1.) - gov) * .001; // Meltdown lifts the governor's ceiling
            double fb = fbS * (p.freeze ? 1.0 / std::max (.3, gov) : 1) * gov, sp = p.spread * .5;
            if (p.freeze || meltHold) fb = std::max (fb, .999);
            double lL = yL * (1 - sp) + yR * sp, lR = yR * (1 - sp) + yL * sp;
            if (p.mode == M_PINGPONG) { const double t2 = lL; lL = lR; lR = t2; } // repeats swap sides every pass
            // ---- in-loop processing
            const double shift = p.pitch + (insEv == 2 ? (R() < .5 ? 12 : -12) : 0) + meltPitch;
            if (p.shimmer > 0 || insEv == 2 || meltPitch != 0) { double sL2, sR2; pitchShift (lL, lR, shift, sL2, sR2); const double m = (insEv == 2 || meltPitch != 0) ? 1 : p.shimmer; lL = lL * (1 - m) + sL2 * m; lR = lR * (1 - m) + sR2 * m; }
            else pitchShiftIdle (lL, lR);
            if (p.diffuse > 0) diffuse (lL, lR, p.diffuse);
            if (p.mode == M_SPACE) { tpL += tapeLc * .7 * (lL - tpL); tpR += tapeLc * .7 * (lR - tpR); lL = std::tanh (tpL * 2.0) / 1.6; lR = std::tanh (tpR * 2.0) / 1.6; } // worn tape: darker, more saturated
            if (p.mode == M_FILTER) { // resonant 4-pole low-pass in the loop, swept by its own LFO (the analogue filter-delay sound)
                fph += p.flfo / sr; if (fph >= 1) fph -= 1; const double fc = std::min (sr * .4, toneS * std::pow (2., (std::sin (2 * M_PI * fph)) * p.fdepth * 2.5));
                const double g2 = 1 - std::exp (-2 * M_PI * fc / sr), k = p.fres * 3.9;
                for (int c = 0; c < 2; ++c) { double& x = c ? lR : lL; double* st = c ? ldR : ldL; const double u = std::tanh (x - k * st[3]);
                    st[0] += g2 * (u - st[0]); st[1] += g2 * (st[0] - st[1]); st[2] += g2 * (st[1] - st[2]); st[3] += g2 * (st[2] - st[3]); x = st[3] * (1 + k * .35); } }
            if (p.mode == M_TAPE) { // age: a little more wow, flutter, saturation and top loss every pass
                tpL += tapeLc * (lL - tpL); tpR += tapeLc * (lR - tpR); lL = std::tanh (tpL * 1.6) / 1.4; lR = std::tanh (tpR * 1.6) / 1.4; }
            if (p.drive > 0) { const double kd = 1 + p.drive * 6; lL = std::tanh (lL * kd) / std::pow (kd, .55); lR = std::tanh (lR * kd) / std::pow (kd, .55); }
            if (foldA > 0) { const double kf = 1 + 5 * foldA, m = foldA * .75; lL = lL * (1 - m) + std::sin (lL * kf) * m; lR = lR * (1 - m) + std::sin (lR * kf) * m; } // wavefolder: Meltdown past 0.2
            if (crushE > 0) {
                if (crushE != crQ) { crQ = crushE; crStep = std::pow (2., 12 - 10 * crushE); }
                crPh += 1 - .97 * crushE; if (crPh >= 1) { crPh -= 1; crHL = std::round (lL * crStep) / crStep; crHR = std::round (lR * crStep) / crStep; }
                lL = lL * (1 - crushE) + crHL * crushE; lR = lR * (1 - crushE) + crHR * crushE; }
            if (p.mode == M_BBD) { // bucket brigade: the clock drops with longer times, so it aliases and darkens; hiss and a compander
                bbdPh += std::min (1., .9 * 48000 / sr * (.375 / std::max (.05, p.time))); if (bbdPh >= 1) { bbdPh -= 1; bbdHL = lL; bbdHR = lR; }
                bbdL += bbdA * (bbdHL - bbdL); bbdR += bbdA * (bbdHR - bbdR); const double hiss = (R() - .5) * .004;
                lL = std::tanh ((bbdL + hiss) * 1.4) / 1.25; lR = std::tanh ((bbdR + hiss) * 1.4) / 1.25; }
            if (p.mode == M_SPECTRAL) { const double oL = sb0 * lL + sz1L, oR = sb0 * lR + sz1R; sz1L = -sa1 * oL + sz2L; sz2L = sb2 * lL - sa2 * oL; sz1R = -sa1 * oR + sz2R; sz2R = sb2 * lR - sa2 * oR; lL = oL * 2.2; lR = oR * 2.2; }
            if (p.loCut > 21) { lcHpL = lcA * lcHpL + (1 - lcA) * lL; lcHpR = lcA * lcHpR + (1 - lcA) * lR; lL -= lcHpL; lR -= lcHpR; }
            const double fL = std::tanh (lL * fb * fbK) / 1.1, fR = std::tanh (lR * fb * fbK) / 1.1; // hotter loop gain with Meltdown: runs into self-oscillation
            const double xL = inL[i], xR = inR[i];
            // ducking: the echo gets out of the way while you play
            const double din = std::max (std::abs (xL), std::abs (xR)); duckEnv += (din > duckEnv ? ka : kr) * (din - duckEnv);
            // Dynamic mode: a strong, smooth duck on both the repeats and the feedback, so the echoes bloom only in the gaps
            const double duckAmt = p.mode == M_DYNAMIC ? std::max (p.duck, .85) : p.duck;
            if (p.mode == M_DYNAMIC) dynEnv += ((din > dynEnv) ? .003 : .00004) * (din - dynEnv);
            const double duckG = 1 - duckAmt * std::min (1., (p.mode == M_DYNAMIC ? dynEnv * 6 : duckEnv * 3));
            const double sendIn = (p.freeze || meltHold) ? 0 : 1;
            const double snL = sendL ? (double) sendL[i] : xL, snR = sendR ? (double) sendR[i] : xR; // a separate send (the plunger feeding only the echo) or the chain itself
            double inSL = snL * sendS * sendIn, inSR = snR * sendS * sendIn;
            if (p.mode == M_PINGPONG) { inSL = (snL + snR) * .5 * sendS * sendIn; inSR = 0; }
            w = (w + 1) & M; iL[(size_t) w] = (float) inSL; iR[(size_t) w] = (float) inSR; bL[(size_t) w] = (float) fL; bR[(size_t) w] = (float) fR;
            const double outG = mixS * duckG;
            const double pL = 1 - std::max (0., panS), pR = 1 + std::min (0., panS); // Meltdown scatters each repeat across the stereo field
            oL[i] = (float) (xL + yL * outG * pL); oR[i] = (float) (xR + yR * outG * pR);
            { const float wv = (float) std::max (std::abs (yL * outG), std::abs (yR * outG)), iv = (float) std::max (std::abs (inSL), std::abs (inSR)); if (wv > wetPk) wetPk = wv; if (iv > inPk) inPk = iv; }
        }
    }

public:
    static constexpr int N = 262144, M = N - 1; double fph = 0, ldL[4] {}, ldR[4] {}, dynEnv = 0;
    double sr; std::vector<float> iL, iR, bL, bR; int w = 0;
    double d1A, d1B, d2A, d2B, xf = 1; long long stepC = 0; long long k = 0;
    double fbS = .4, fbT = .4, toneS = 5000, toneT = 5000, lpL = 0, lpR = 0, hpL = 0, hpR = 0, env = 0, gov = 1, wph = 0, flph = 0, wdr = 0, sendS = 1, thrG = 0; int thrSteps = 0; double mixS = .3, curSnd = 1;
    // new setups' state
    double sb0 = 0, sb2 = 0, sa1 = 0, sa2 = 0, sz1L = 0, sz2L = 0, sz1R = 0, sz2R = 0; int specIdx = 0; double tapStepT = 0, tapLv[3] { .5, .35, .6 }, tapPan[3] { -.6, .7, -.2 };
    double bbdPh = 0, bbdHL = 0, bbdHR = 0, bbdL = 0, bbdR = 0, bbdA = .3; long long grT[2] { 1, 1 }; double grLen = 2400, grOff[2] {}, grRt[2] { 1, 1 }, grP[2] {};
    // Echo+ state
    double exS = 0; double wsS = 0, wsT = 0, panS = 0, panT = 0, meltPitch = 0, crQ = -1, crStep = 4096; bool meltRev = false, meltHold = false;
    int insEv = 0; long long revStart = 0; double revLen = 1; double lc = 0, lcTone = -1; int lcCount = 0; double lcA = 0, tapeLc = 0, tpL = 0, tpR = 0, crPh = 0, crHL = 0, crHR = 0, lcHpL = 0, lcHpR = 0, duckEnv = 0;
    static constexpr int PSN = 16384; std::vector<float> psL, psR; int psW = 0, psWin = 2880; double psPh = 0;
    std::vector<float> apL[4], apR[4]; int apN[4] {}, apI[4] {};
    // pitch shifter: two read heads sweeping a short window, crossfaded (classic delay-line shifter)
    void pitchShift (double xL, double xR, double semis, double& yL, double& yR) {
        psL[(size_t) psW] = (float) xL; psR[(size_t) psW] = (float) xR;
        const double ratio = std::pow (2., semis / 12); psPh += (1 - ratio) / psWin; psPh -= std::floor (psPh);
        double sL = 0, sR = 0;
        for (int h = 0; h < 2; ++h) { const double ph = std::fmod (psPh + h * .5, 1.), d = 2 + ph * psWin, g = std::sin (M_PI * ph);
            double pos = psW - d; if (pos < 0) pos += PSN; const int i0 = (int) pos; const double f = pos - i0; const int i1 = (i0 + 1) % PSN;
            sL += g * (psL[(size_t) i0] * (1 - f) + psL[(size_t) i1] * f); sR += g * (psR[(size_t) i0] * (1 - f) + psR[(size_t) i1] * f); }
        yL = sL; yR = sR; psW = (psW + 1) % PSN;
    }
    void pitchShiftIdle (double xL, double xR) { psL[(size_t) psW] = (float) xL; psR[(size_t) psW] = (float) xR; psW = (psW + 1) % PSN; }
    // diffusion: four allpasses per side (different lengths left and right) smear repeats into a wash
    void diffuse (double& L, double& Rch, double amt) {
        const double g = .35 + .4 * amt; double xl = L, xr = Rch;
        for (int j = 0; j < 4; ++j) {
            auto& bl = apL[j]; auto& br = apR[j]; const int nl = apN[j], nr = apN[j] + 37; int& ii = apI[j];
            const double dl = bl[(size_t) (ii % nl)], dr = br[(size_t) (ii % nr)];
            const double vl = xl + g * dl, vr = xr + g * dr; bl[(size_t) (ii % nl)] = (float) vl; br[(size_t) (ii % nr)] = (float) vr;
            xl = dl - g * vl; xr = dr - g * vr; ii = (ii + 1) % (nl * nr);
        }
        L = L * (1 - amt) + xl * amt; Rch = Rch * (1 - amt) + xr * amt;
    }
    struct Pat { double fb, tone, jit, thr, snd = 1; }; Pat pat[16]; uint32_t a = 7; Rng R;
    double rnd() { uint32_t t = (a += 0x6D2B79F5u); t = (t ^ (t >> 15)) * (t | 1u); t ^= t + (t ^ (t >> 7)) * (t | 61u); return (double) (t ^ (t >> 14)) / 4294967296.0; }
    void genPat() { a = (uint32_t) (p.seed * 2246822519u); if (! a) a = 1; for (auto& q : pat) { q.fb = .6 + .8 * rnd(); q.tone = .3 + 1.3 * rnd(); q.jit = rnd() * 2 - 1; q.thr = rnd(); q.snd = rnd() < .3 ? .15 + .3 * rnd() : .7 + .3 * rnd(); }
                    for (int j = 0; j < 3; ++j) { tapLv[j] = rnd() < .25 ? 0 : .25 + .55 * rnd(); tapPan[j] = rnd() * 2 - 1; } }
    double rd (const std::vector<float>& b, double pos) const { const double fl = std::floor (pos); const long long i = (long long) fl; const double f = pos - fl; return b[(size_t) (i & M)] * (1 - f) + b[(size_t) ((i + 1) & M)] * f; }
    double off (long long q, double T) const { return ((((q % 2) + 2) % 2) ? p.swing * .33 * T : 0) + pat[(size_t) (((q % 16) + 16) % 16)].jit * p.groove * .012 * sr; }
};

} // namespace pm
