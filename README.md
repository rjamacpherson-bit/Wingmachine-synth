# Wing Machine (VST3) — Feather Synth

A C++/JUCE port of the Wing Machine web instrument (v8.2): two coupled wings flap by themselves, tempo-locked to the host, and every
flap becomes a feather-voice pluck, a thump or a tick. A creature (19 of them) sets the whole patch; behaviors, seasons, weather and
disasters reshape it. Everything runs in `Source/engine/` with no JUCE dependency; `Source/Plugin*.cpp` is the thin VST3 wrapper.

## Build it (Windows, no installs on your machine)
1. Create a new GitHub repository and push this folder to it.
2. Open the repository's **Actions** tab, run **Build Wing Machine VST3 (Windows)** (it also runs on every push).
3. When it finishes (about 10 minutes the first time), download the artifact `WingMachine-win64-vst3`, unzip it, and copy
   `Wing Machine.vst3` (a folder) to `C:\Program Files\Common Files\VST3\`.
4. Rescan plugins in your DAW. It appears as an **instrument** called *Wing Machine* (manufacturer "Feather Synth").

The workflow also runs `engine_test.exe quick` (renders every creature, season, behavior, weather and event, and fails the build on a NaN or clipping).

## Build it locally (Windows)
Install Visual Studio 2022 (Desktop C++ workload) and CMake, then:
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
The plugin ends up in `build\WingMachine_artefacts\Release\VST3\`. The first configure downloads JUCE 8.0.4 from GitHub.

## Using it
- Put it on an instrument track and **press play**. With the transport running, the wings lock to the host's beat grid; stopped, they run
  freely at the host tempo. MIDI is ignored in this first build.
- The wing sounds are the instrument. Your own drums come from other tracks; the wing thump is a light tom by design.
- **Quantize to host grid** pulls every wing sound toward the nearest 16th (with **Grid swing** matching your drums), **Beat lock** pulls the left downstroke onto the grid.
- Creature, Behavior, Season, Weather and the nine Events are all automatable choices/toggles. Pick the Simurgh and a swarm of thirty starts by itself.
- The FX amounts (Dub, Dub feedback, Echo, Air, Room) are **trims** around each creature's own values: 0 = the creature's own setting, -1 half, +1 double.
- **Tame** (default 0.6) ducks and levels the echo return and pulls back feedback and wet levels. 0 = off.
- **Output** is a plain trim after the master compressor.
- The status line at the top shows what the engine is doing (season state, fusion, thorax temperature, phoenix phase and so on).

## What differs from the web version
- No internal drum loop (the host plays drums). The wing sounds quantize to the host's grid instead.
- The environment bus blur uses the shelf's reverb instead of a convolution reverb.
- The two Web Audio compressors are re-created (same curve and automatic make-up gain) but without their lookahead, so the plugin reports zero latency.
- The classic saw-wave wing voices and the canvases/cards are not ported.
- Control rate is every 64 samples instead of every animation frame, so events land on exact samples.

## Not yet verified
- It has been compiled and run offline with g++ (the engine, rendered through every creature and effect) and syntax-checked against JUCE 8.0.4 headers, but **not yet built with MSVC or loaded in a DAW**. The first CI run is the real test. Please send me any compiler errors from the Actions log.
- Loudness has not been matched by ear against the web version (the Output knob is there for that).

## Licence note
JUCE 8 is dual-licensed (AGPLv3 or commercial). Fine for your own testing; check the terms before distributing builds.
The Feather Shelf headers in `Source/engine/` (DubCore, XSpace, Stretch, CrushCore, ReverbCore) are your Plunger Model FX code; `Stretch.h` and `DubCore.h` had `private:` changed to `public:` so the engine can read their play positions.
