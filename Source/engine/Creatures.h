// Wing Machine engine: the creature table (properties) and the sound tables (derive: properties -> patch parameters).
#pragma once
#include "Dsp.h"

namespace wm {

enum Special { SP_NONE, SP_HUM, SP_SILENT, SP_SOAR, SP_ALT, SP_ECHO, SP_BOOM, SP_BUZZ, SP_WARM, SP_SCALES, SP_PLAGUE, SP_FLASH, SP_THIRTY, SP_PHOENIX, SP_THUNDER, SP_TWOSCALE, SP_WINDOW };
enum Style { ST_HOVER, ST_STEADY, ST_SOAR, ST_FLAPGLIDE, ST_WAGGLE, ST_SWAY };
enum HabId { H_GARDEN, H_HIVE, H_POND, H_FOREST, H_OCEAN, H_OPEN, H_CAVE, H_SAVANNA, H_SKY };
enum ScaleId { SC_LYDPENT, SC_MIXO, SC_WHOLE, SC_MINPENT, SC_FIFTHS, SC_PENT, SC_DORIAN, SC_LINYANG, SC_LINYIN };

struct Creature {
    const char* id; const char* name; double mass, hz, ar; int style; double coord, stiff, loud; int hab, scale, scaleB, dub; double rateMul; int special; double call;
};

inline const std::vector<Creature>& creatures() {
    static const std::vector<Creature> C = {
        { "hummingbird", "Hummingbird", 3.4, 43, 10.6, ST_HOVER, 20, .65, .5, H_GARDEN, SC_LYDPENT, -1, 12, 1, SP_HUM, 0 },
        { "bee", "Honeybee", .08, 230, 5, ST_WAGGLE, 0, .7, .45, H_HIVE, SC_MIXO, -1, 6, 1, SP_HUM, 0 },
        { "dragonfly", "Dragonfly", .754, 36, 5.6, ST_HOVER, 180, .7, .5, H_POND, SC_WHOLE, -1, 11, 1, SP_HUM, 0 },
        { "owl", "Owl", 470, 6, 7, ST_STEADY, 15, .15, .28, H_FOREST, SC_MINPENT, -1, 0, 1, SP_SILENT, 0 },
        { "albatross", "Albatross", 9400, 4.2, 15, ST_SOAR, 10, .55, .55, H_OCEAN, SC_FIFTHS, -1, 1, 1, SP_SOAR, 0 },
        { "pigeon", "Crested pigeon", 200, 8, 6, ST_FLAPGLIDE, 20, .6, .5, H_OPEN, SC_PENT, -1, 2, 1, SP_ALT, 0 },
        { "bat", "Little brown bat", 7.1, 10, 6.4, ST_STEADY, 15, .3, .45, H_CAVE, SC_DORIAN, -1, 4, 1, SP_ECHO, 45000 },
        { "ostrich", "Ostrich", 100000, .8, 3, ST_SWAY, 180, .25, .55, H_SAVANNA, SC_FIFTHS, -1, 9, .6, SP_BOOM, 0 },
        { "bumblebee", "Bumblebee", .17, 180, 5.5, ST_HOVER, 20, .7, .45, H_GARDEN, SC_MIXO, -1, 6, 1, SP_BUZZ, 0 },
        { "hawkmoth", "Hawkmoth", 2, 25.2, 4, ST_STEADY, 15, .35, .4, H_FOREST, SC_DORIAN, -1, 4, 1, SP_WARM, 0 },
        { "monarch", "Monarch", .5, 10, 3.5, ST_FLAPGLIDE, 10, .2, .35, H_OPEN, SC_LYDPENT, -1, 2, 1, SP_SCALES, 0 },
        { "locust", "Locust", 2, 22.9, 4.5, ST_STEADY, 25, .5, .45, H_SAVANNA, SC_MINPENT, -1, 9, 1, SP_PLAGUE, 0 },
        { "firefly", "Firefly", .03, 2, 3, ST_STEADY, 0, .3, .4, H_FOREST, SC_WHOLE, -1, 12, 1, SP_FLASH, 0 },
        { "simurgh", "Simurgh", 3000, 3, 8, ST_SOAR, 10, .5, .55, H_SKY, SC_PENT, -1, 1, 1, SP_THIRTY, 0 },
        { "phoenix", "Phoenix", 5000, 4, 7, ST_STEADY, 20, .55, .55, H_CAVE, SC_MINPENT, -1, 3, 1, SP_PHOENIX, 0 },
        { "thunderbird", "Thunderbird", 40000, 1.5, 6, ST_SOAR, 10, .5, .6, H_OCEAN, SC_FIFTHS, -1, 1, 1, SP_THUNDER, 0 },
        { "fenghuang", "Fenghuang", 8000, 3, 9, ST_FLAPGLIDE, 180, .6, .5, H_POND, SC_LINYANG, SC_LINYIN, 11, 1, SP_TWOSCALE, 0 },
        { "garuda", "Garuda", 60000, 1.5, 8, ST_STEADY, 180, .55, .6, H_SKY, SC_DORIAN, SC_MIXO, 9, 1, SP_TWOSCALE, 0 },
        { "icarus", "Icarus", 80000, 1.2, 6, ST_STEADY, 30, .1, .4, H_OCEAN, SC_LYDPENT, -1, 4, 1, SP_WINDOW, 0 }
    };
    return C;
}

inline const std::vector<double>& scaleOf (int id) {
    static std::vector<std::vector<double>> S;
    if (S.empty()) {
        S = { { 1, 9. / 8, 5. / 4, 45. / 32, 3. / 2 }, { 1, 9. / 8, 5. / 4, 4. / 3, 3. / 2, 5. / 3, 16. / 9 }, { 1, 9. / 8, 5. / 4, 45. / 32, 25. / 16, 225. / 128 },
              { 1, 6. / 5, 4. / 3, 3. / 2, 9. / 5 }, { 1, 9. / 8, 4. / 3, 3. / 2, 27. / 16 }, { 1, 9. / 8, 81. / 64, 3. / 2, 27. / 16 }, { 1, 9. / 8, 6. / 5, 4. / 3, 3. / 2, 5. / 3, 9. / 5 } };
        // Ling Lun's pipes: multiply by 2/3 then 4/3 alternately; fold into one octave; even steps yang, odd steps yin
        std::vector<double> yang, yin; double x = 1;
        for (int n = 0; n < 12; ++n) { if (n > 0) x *= (n % 2 == 1) ? 2. / 3 : 4. / 3; double r = x; while (r < 1) r *= 2; while (r >= 2) r /= 2; (n % 2 == 0 ? yang : yin).push_back (r); }
        std::sort (yang.begin(), yang.end()); std::sort (yin.begin(), yin.end()); S.push_back (yang); S.push_back (yin);
    }
    return S[(size_t) id];
}

struct HabRow { double decay, room; int algo; };
inline HabRow habOf (int h) {
    static const HabRow T[] = { { 1.6, .18, 3 }, { .8, .2, 1 }, { 2.4, .2, 6 }, { 3.8, .26, 5 }, { 6, .3, 2 }, { 1.8, .18, 4 }, { 4.5, .3, 8 }, { 2.2, .12, 2 }, { 7, .34, 2 } };
    return T[h];
}

struct Derived {
    int index = 0; const Creature* c = nullptr;
    double M = 1, root = 440, mul = 2, humF = 0, split = .14, ring = .5, gamma = PI; bool hum = false, gate = true, restGlide = true;
    int burst = 6, algo = 2, dub = 0, special = SP_NONE; double decay = 2, room = .15, rvSize = .6, vlevel = .5, rateMul = 1, dubMix = .25, dfb = .45, echo = .35, air = .25, pan = .5, kickAmp = 1, kickDiv = 1;
    const std::vector<double>* sc = nullptr; const std::vector<double>* scB = nullptr;
};

inline double ptone (double f) { return 440 * std::pow (2.0, std::round (12 * std::log2 (f / 440)) / 12); }
inline double snapScale (double f, double root, const std::vector<double>& sc) {
    double best = f, bd = 1e9;
    for (int o = -6; o <= 6; ++o) for (double r : sc) { const double g = root * r * std::pow (2.0, o), d = std::abs (std::log2 (g / f)); if (d < bd) { bd = d; best = g; } }
    return best;
}

inline Derived derive (int index, double sizefx) {
    const Creature& c = creatures()[(size_t) index]; Derived d; d.index = index; d.c = &c;
    const double D = sizefx, M = std::pow (c.mass / 50, .25); d.M = M;
    auto ms = [&] (double x) { return std::pow (M, x * D); };
    d.root = c.call > 0 ? ptone (foldHz (c.call, 200, 500)) : ptone (440 * std::pow (M / .51, -.6 * D));
    d.sc = &scaleOf (c.scale); d.scB = c.scaleB >= 0 ? &scaleOf (c.scaleB) : nullptr;
    if (c.hz >= 25) { d.mul = 3; d.hum = true; d.humF = snapScale (foldHz (c.hz, 200, 800), d.root, *d.sc); }
    else d.mul = c.hz < 2 ? 1 : c.hz < 5 ? 1.5 : c.hz < 9 ? 2 : 3;
    d.split = clampd (.24 - .012 * c.ar, .07, .2); d.ring = clampd (c.stiff * ms (.35), .05, .95); d.gamma = c.coord * PI / 180;
    static const bool GATE[] = { false, false, true, true, true, true }; static const int BURST[] = { 6, 6, 3, 6, 8, 10 };
    d.gate = GATE[c.style]; d.burst = BURST[c.style]; d.restGlide = c.style != ST_WAGGLE;
    const HabRow h = habOf (c.hab); d.algo = h.algo; d.decay = h.decay * ms (.45); d.room = clampd (h.room * ms (.3), .04, .6); d.rvSize = clampd (.55 + .18 * std::log2 (M) * D, .15, 1);
    d.dub = c.dub; d.vlevel = clampd (c.loud * 1.1 * ms (.2), .15, .9); d.rateMul = c.rateMul;
    d.dubMix = clampd (.25 * ms (.35), .08, .7); d.dfb = clampd (.45 * ms (.2), .25, .8); d.echo = clampd (.35 * ms (.2), .12, .7); d.air = clampd (.25 * ms (.35), .06, .7);
    d.pan = clampd (.25 + .6 * clampd (M / 2, 0, 1), .2, .85); d.kickAmp = clampd (ms (.35), .2, 2.2); d.kickDiv = clampd (ms (.3), .6, 2.2);
    d.special = c.special; return d;
}

inline const char* modeName (int i) { static const char* N[] = { "Working", "Attacking", "Escaping", "Defending", "Nesting" }; return N[i]; }

} // namespace wm
