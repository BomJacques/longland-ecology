# Ecology architecture

## Audited baseline

The supplied task workspace was empty. The JUCE source was subsequently identified at `C:/SDK/JUCE`; its CMake project reports JUCE 9.0.0. The build is CMake-first, C++20, and currently emits a VST3 target on Windows. Standalone work is paused.

There was no prior plugin format, DSP implementation, state model, analyzer, test framework, or repository code to preserve.

## Current Phase 5 boundary

The current binary contains:

```text
source audio
  -> source ResourceAnalyzer
  -> validated 50 Hz Predator/Prey model
       Prey role = Bloom population
  -> real Predator oversampled saturation branch
  -> real Bloom stereo diffusion/FDN branch
  -> Bloom organism bus -> real Parasite dual-head microshift/smear
  -> physically modified Bloom bus -> real Scavenger fractional delay
  -> Predator branch -> real Grazer adaptive three-band attenuation
  -> dedicated pre/post branch ResourceAnalyzers
  -> measured Bloom branch and source transients feed Predator next frame
  -> measured Bloom tails/repetition feed Scavenger
  -> measured Bloom activity/sustain feed Parasite
  -> gain-matched pre/post Parasite difference creates host pressure
  -> measured Scavenger return tails support Bloom
  -> measured physical Grazer high-band consumption suppresses Predator food
  -> Predator attack suppresses Bloom
  -> habitat ResourceAnalyzer
  -> smoothed, latency-aligned, bounded wet/dry mix
```

`LONGLAND_PHASE2_GATE_ACCEPTED` records the reviewed gate decision. Configuring real DSP while that flag is false is rejected.

## State separation

- `ResourceAnalyzer` measures audio and knows nothing about species.
- `ResourceMap` contains normalized resources derived from actual samples.
- `EcologyEngine` owns generic fixed-capacity species and relationship state.
- `PredatorPreyModel` remains the canonical Phase 2-5 production population core.
- `Phase4EcologyModel` wraps that core with bounded Scavenger and Grazer state rather than replacing it.
- `Phase5EcologyModel` wraps Phase 4 with bounded Parasite state and explicit measured-host dependence.
- `DummyGainProcessor` remains only in the Phase 2 regression harness.
- `PredatorProcessor` provides 2x FIR oversampling, smoothed drive, compensation, DC rejection, and a bounded output.
- `BloomProcessor` provides stereo diffusion and a four-line feedback-delay network capped below unity.
- `ScavengerProcessor` provides a wet-only interpolated stereo delay with lossy cross-feedback and filtering.
- `GrazerProcessor` provides stereo-coherent low/mid/high detection and smooth bounded attenuation.
- `ParasiteProcessor` provides a bounded, smoothed, dual-head stereo microshift with smear and lossy feedback.
- `PluginProcessor` coordinates exact control windows and publishes atomic telemetry.
- `PluginEditor` reads telemetry only; it does not invent animation or modify population.

Fixed capacities in the generic model are eight species and 64 edges. No ecology update resizes containers or performs file I/O. The JUCE processor preallocates its audio buses in `prepareToPlay()` and returns a safe dry pass-through if a host violates the prepared maximum block contract.

## Causal ordering

At each 20 ms control window:

1. Audio is rendered through population-derived Predator, Bloom, Parasite, Scavenger, and Grazer targets.
2. Source and each attributable organism branch are measured independently.
3. Bloom activity/source transients feed Predator; Bloom tails feed Scavenger; pre/post Grazer high energy records physical consumption; Scavenger return tails create support; Bloom sustain feeds Parasite; gain-matched pre/post Parasite energy records physical host modification.
4. The bounded model advances once.
5. New populations become gain targets for the next window.

This one-frame delay prevents algebraic feedback and makes the causal lag testable. The plugin splits arbitrary host blocks at control boundaries; ecology timing is therefore sample-count based rather than process-block based.

## Canonical Phase 2 equations

For each population `p` with non-negative birth/death coefficients `B` and `D`:

```text
dp/dt = B(1 - p) - Dp
p∞ = B / (B + D)
pNext = p∞ + (p - p∞) exp(-(B + D) dt)
```

This exact per-tick solution preserves `[0, 1]` for finite non-negative rates. A final finite/range check retains a last-known-good snapshot and counts numerical recoveries.

The Phase 5 production loop uses the same core equations. Bloom occupies the validated Prey role; its base food comes from source silence/sparsity, with separately measured Scavenger support and Parasite pressure edges. Parasite cannot grow or produce DSP intensity without both viable Bloom ecology and measured Bloom audio. Its pressure is zero unless its rendered pre/post audio difference is also nonzero. There is no oscillator, wall-clock branch, recorded envelope, scripted event, mutation, catastrophe, or RNG in the cycle.

## Resource analysis

The analyzer currently provides activity, transients, sustain, low/mid/high energy, silence, sparsity, tail persistence, repetition approximation, spectral dominance, and dynamic contrast. It:

- updates at sample-count-defined 50 Hz frames;
- retains stereo energy before mono cancellation, so anti-phase stereo does not disappear;
- uses sample-rate-derived envelope and crossover coefficients;
- sanitizes all input and output values;
- allocates no storage from `processBlock()`;
- produces identical results for the tested 64- and 257-sample partitions.

The current spectral implementation uses per-channel lightweight one-pole band separation, so anti-phase stereo retains both energy and spectral identity. A later quality phase may replace it with a preallocated STFT while retaining the same `AnalysisFrame`/`ResourceMap` boundary.

## Thread ownership

The current ecology workload is small enough to remain on the audio thread at 50 Hz, avoiding control-thread races. Parameters are atomically captured by JUCE. GUI telemetry is copied to atomics after processing and read at 20 Hz. No GUI state is read by the ecology engine.

Future editable relationship graphs should use fixed-size POD commands transferred at tick boundaries. They must not introduce mutexes, shared mutable vectors, graph reconstruction, or destruction of heap-owned snapshots on the audio thread.

## Audio safety added in Phase 3

- All parallel paths are aligned to Predator's fixed 15-sample oversampling latency, which is reported to the host.
- Dry/Wet, output gain, and routing weights use 20 ms sample ramps.
- Inputs and automatable values are finite/range sanitized.
- The final mixer uses a smooth ceiling below 0 dBFS.
- Bloom and Scavenger feedback are strictly lossy and the host is told about a conservative 24-second tail.

## Next allowed work

Phase 6 may add Decomposer granular processing. It must remain downstream of the now-tested five-species closed loop.
