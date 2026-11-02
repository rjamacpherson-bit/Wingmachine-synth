// fast non-deterministic uniform random numbers in [0,1) (stands in for Math.random)
#pragma once
#include <cstdint>
namespace pm {
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    Rng() { static uint64_t salt = 0x2545F4914F6CDD1Dull; salt += 0x9E3779B97F4A7C15ull; s ^= salt * 0xBF58476D1CE4E5B9ull; if (! s) s = 1; }
    double operator()() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return (double) ((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0); }
};
}
