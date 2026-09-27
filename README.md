# Longland Ecology - Phase 5 ecology-first VST3 build

> Current source includes experimental Phase 6 work. Verification on 2026-09-27
> built the framework-independent targets and passed 12 of 13 CTest checks.
> `phase6_repetition_audio` failed its population, audible-modification and
> downstream-interaction thresholds. Phase 6 is not validated as working.
> See `KNOWN_ISSUES.md`. The Phase 5 description below remains historical context.

Phase 2 proved the Predator/Prey dynamics with dummy gains. After user review, Phase 3 replaced those dummy processors with real Predator saturation and Bloom ambience. Phase 4 added Scavenger delay and Grazer adaptive filtering. Phase 5 adds Parasite micro-pitch/smear processing whose survival, sound, and pressure all require a measured Bloom host.

It contains:

- a JUCE 9.0.0 VST3 target;
- framework-neutral, allocation-free resource analysis;
- bounded ecological state and relationship primitives;
- the mandatory gain-only Predator/Prey regression gate;
- real 2x-oversampled Predator saturation and bounded stereo Bloom ambience;
- a fractional stereo Scavenger delay fed only by the Bloom organism bus;
- a stereo-coherent Grazer that attenuates the dominant Predator band;
- a dual-head stereo Parasite microshifter inserted serially after Bloom;
- a closed loop where measured Bloom audio feeds Predator, Predator suppresses Bloom, Predator starves, and Bloom recovers;
- measured Bloom tails feed Scavenger, measured Scavenger returns support Bloom, and physical Grazer attenuation deprives Predator;
- measured Bloom audio and stable sustain feed Parasite, while gain-matched pre/post Parasite audio proves physical modification before host pressure is applied;
- visible Resources, Stability, Population Speed, Dry/Wet, and Output controls bound to host state;
- real debug telemetry whose node sizes follow actual population, plus raw resource meters that show what is feeding each relationship;
- deterministic tests, an eight-hour accelerated run, a counterfactual attack test, CSV evidence, and an SVG population plot.

It intentionally does **not** yet contain Decomposer granular processing, mutation/evolution, catastrophe, presets, or Phase 11 host hardening.

## Built artifacts

- `artifacts/Longland Ecology.vst3` - current JUCE VST3 debug bundle
- `ECOLOGY_PHASE2_TRACE.csv` — 300 seconds of deterministic telemetry plus an attack-disabled counterfactual
- `ECOLOGY_PHASE2_TRACE.svg` — population trace plot
- `artifacts/phase3-dry.wav` — deterministic 40-second dry fixture
- `artifacts/phase3-processed.wav` — the same fixture through real Phase 3 ecology
- `artifacts/phase4-dry.wav` - deterministic 64-second Phase 4 source fixture
- `artifacts/phase4-processed.wav` - the same fixture through the four-species ecology

The plugin is not the finished six-species product. Its audio path now contains five real effects, but the ecology remains the controlling system rather than a set of independent wet knobs. Standalone development is currently paused; only the VST3 is rebuilt and packaged.

The Phase 4 listening retune makes Scavenger deliberately audible: short repeated plucks grow it from near-dormancy within roughly ten seconds, while dry transient-only material still starves it. New instances default to higher resources/speed, lower stability, and 72% Dry/Wet; all five values are adjustable on the plugin itself.

## Build

The default local JUCE source is:

```text
C:/SDK/JUCE
```

Override it with `-DLONGLAND_JUCE_ROOT=<path>` on another machine.

From a Visual Studio x64 developer prompt:

```powershell
cmake -S . -B build -G Ninja
cmake --build build --target longland_phase2_gauntlet predator_processor_tests bloom_processor_tests grazer_processor_tests scavenger_processor_tests parasite_processor_tests phase5_ecology_tests phase5_integration_tests phase3_render_cli phase4_render_cli LonglandEcology_VST3
ctest --test-dir build --output-on-failure
```

## Preserved phase boundary

Phase 3 was enabled only after the user reviewed the Phase 2 result. The original gain-only gate remains in the test suite and must continue passing.

1. autonomous under constant input and settings;
2. causally ordered, with Predator following Prey;
3. dependent on the attack relationship;
4. capable of collapse, starvation, and recovery;
5. deterministic and bounded;
6. invariant to host-buffer partitioning at the fixed 50 Hz ecology cadence.

The next allowed product phase is Phase 6: Decomposer and granular processing.

See `ECOLOGY_ARCHITECTURE.md` and `ECOLOGY_GAUNTLET_RESULTS.md` for details.
