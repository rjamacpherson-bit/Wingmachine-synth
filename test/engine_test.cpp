// Offline test of the engine without JUCE: renders every creature and season at 120 BPM, checks for NaNs, peaks, and a click metric.
#include "../Source/engine/WingEngine.h"
#include <cstdio>
#include <cmath>
using namespace wm;
int main (int argc, char** argv) {
    const bool quick = argc > 1;
    const double sr = 48000; WingEngine e; e.prepare (sr);
    const int B = 480; std::vector<float> L (B), R (B); HostInfo h; h.bpm = 120; h.playing = true; h.hasPpq = true;
    int bad = 0; double ppq = 0;
    auto run = [&] (const char* name, Settings s, double secs) {
        double pk = 0, sum = 0; long n = 0; bool nan = false;
        for (int b = 0; b < (int) (secs * sr / B); ++b) { h.ppq = ppq; e.process (s, h, L.data(), R.data(), B); ppq += B / (sr * 0.5);
            for (int i = 0; i < B; ++i) { const double a = L[i], c = R[i]; if (! std::isfinite (a) || ! std::isfinite (c)) nan = true; pk = std::max (pk, std::max (std::abs (a), std::abs (c))); sum += a * a + c * c; ++n; } }
        char st[400]; e.getStatus (st, sizeof st);
        std::printf ("%-26s rms %.3f pk %.2f %s | %s\n", name, std::sqrt (sum / (2.0 * n)), pk, nan ? "NaN!" : "ok", st); if (nan || pk > 1.6) ++bad;
    };
    Settings s;
    for (int c = 0; c < 19; ++c) { s.creature = c; char nm[64]; std::snprintf (nm, sizeof nm, "creature %d %s", c, creatures()[(size_t) c].name); run (nm, s, quick ? 2 : 6); }
    s.creature = 0;
    const char* seas[] = { "none", "swarm", "migrate", "pillage", "mating", "bloom" };
    for (int q = 1; q < 6; ++q) { s.season = q; s.creature = q == 5 ? 8 : 1; run (seas[q], s, quick ? 4 : 10); }
    s.season = 0; s.creature = 3;
    for (int m = 0; m < 5; ++m) { s.mode = m; char nm[32]; std::snprintf (nm, sizeof nm, "mode %s", modeName (m)); run (nm, s, quick ? 2 : 5); }
    s.mode = 0;
    for (int w = 1; w < 8; ++w) { s.weather = w; char nm[32]; std::snprintf (nm, sizeof nm, "weather %d", w); run (nm, s, quick ? 2 : 5); }
    s.weather = 0;
    for (int ev = 0; ev < 9; ++ev) { s.events[ev] = true; char nm[32]; std::snprintf (nm, sizeof nm, "event %d", ev); run (nm, s, quick ? 2 : 6); s.events[ev] = false; }
    for (int ev = 0; ev < 9; ++ev) s.events[ev] = true; s.weather = 4; s.season = 1; s.elevel = 1; run ("everything", s, quick ? 3 : 8);
    std::printf (bad ? "FAILED %d\n" : "ALL OK\n", bad); return bad ? 1 : 0;
}
