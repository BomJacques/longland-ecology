# Ecology gauntlet results

Run date: 2026-08-30  
JUCE: 9.0.0  
Compiler: MSVC 19.51.36252  
Active build product: VST3 debug build (Standalone work paused)

## Phase 1 resource-analysis checks

Passed:

- silence produces high Silence and negligible Activity;
- an impulse train creates more Transient resource and more Sparsity than a sustained tone;
- 80 Hz, 1 kHz, and 8 kHz fixtures dominate low, mid, and high resources respectively;
- anti-phase stereo retains activity because channel powers are accumulated before averaging;
- identical samples partitioned into 64- and 257-sample host blocks produce identical resource frames.

## Mandatory Phase 2 gate

Command:

```text
ctest --test-dir work/build-longland-ninja -L phase2_gate --output-on-failure
```

Result:

```text
1/1 phase2_gain_only_gate passed
100% tests passed, 0 failed
```

The 300-second constant-input run produced:

| Metric | Result |
|---|---:|
| Prey population range | 0.0112–0.8954 |
| Predator population range | 0.0000037–0.8383 |
| Prey peaks after burn-in | 15 |
| Predator peaks after burn-in | 15 |
| Mean cycle period in exported trace | 17.50 s |
| Mean Predator lag after Prey | 2.90 s |
| Late Prey increase when attack alone is disabled | 0.5405 |
| Numerical faults in accelerated 8-hour run | 0 |

## What these results establish

- Constant input and constant settings create repeated cycles without an LFO or scripted timeline.
- Predator growth follows measured gain-only Prey-branch activity.
- Predator attack drives Prey downward.
- Lower Prey activity starves Predator.
- Predator decline permits Prey recovery.
- Removing only the attack edge materially changes the trajectory, demonstrating that the graph relationship is causal rather than decorative.
- Repeating the run produces exactly identical population arrays.
- The fixed 50 Hz coordinator produces identical trajectories for 64- and 257-sample host partitions.
- Population integration remains finite and bounded during an accelerated eight-hour simulation.

The exported `ECOLOGY_PHASE2_TRACE.csv` contains every tick and the attack-disabled counterfactual. `ECOLOGY_PHASE2_TRACE.svg` plots Prey, Predator, and counterfactual Prey.

## Deliberately not claimed

- Decomposer granular processing does not exist yet.
- The lightweight band analyzer is not yet an FFT-grade production analyzer.
- Plugin validation, automation torture, multiple-instance testing, sanitizer runs, and an actual eight-hour wall-clock audio render remain later hardening tasks.
- The present two-node view is a truthful development display, not the final ecosystem GUI.

Phase 2 was numerically passed and reviewed before Phase 3 began, preserving the requested ecology-first development order.

## Phase 3 results

Phase 3 was started after user acceptance of the Phase 2 evidence. The dummy-gain gate remains unchanged and passing.

Focused tests:

- Predator: aligned bypass, hostile/non-finite input, bounded output, harmonic generation, smoothed automation, and DC decay.
- Bloom: 12-second maximum-decay impulse, finite/bounded output, tail creation, reset clearing, invalid controls, and parameter-jump smoothing.
- Integrated render: actual analyzers, validated ecology, real Predator/Bloom DSP, exact 50 Hz causal updates, and WAV output acceptance thresholds.

Latest complete CTest run:

```text
4/4 tests passed
phase2_gain_only_gate      passed
phase3_predator_audio      passed
phase3_bloom_audio         passed
phase3_closed_loop_render  passed
```

The 40-second integrated real-DSP fixture produced:

| Metric | Result |
|---|---:|
| Predator population range | 0.000001–0.793357 |
| Bloom population range | 0.054613–0.858716 |
| Predator routed intensity | 0.000000–0.792942 |
| Bloom routed intensity | 0.015660–0.924338 |
| Dry/processed difference RMS | 0.041766 |

The fixture alternates transients, sparse plucks, a sustained pad, more transients, and eight seconds of silence. `artifacts/phase3-dry.wav` and `artifacts/phase3-processed.wav` are directly comparable.

## Adversarial corrections

Fresh audio and ecology reviewers identified and prompted corrections for:

- accidental replacement of the validated Phase 2 model;
- one-way decorative suppression;
- 15-sample parallel-path misalignment;
- hard clipping above 0 dBFS;
- unsmoothed routing/master controls;
- non-finite final-mix escape;
- missing Bloom tail declaration;
- anti-phase stereo losing spectral-band identity;
- Unicode UI glyph corruption and fitted-text layout.

The production loop now retains the validated model: measured Bloom audio feeds Predator, Predator attack suppresses Bloom, Bloom collapse starves Predator, and recovery remains possible.

## Phase 4 results

Phase 4 adds real Scavenger delay and Grazer spectral processing. Dedicated branch analyzers ensure their food and relationship effects come from rendered audio rather than routing weights or GUI state.

Latest complete CTest run:

```text
7/7 tests passed
phase2_gain_only_gate       passed
phase3_predator_audio       passed
phase3_bloom_audio          passed
phase4_grazer_audio         passed
phase4_scavenger_audio      passed
phase3_closed_loop_render   passed
phase4_closed_loop_render   passed
```

The 64-second Phase 4 real-audio fixture produced:

| Metric | Result |
|---|---:|
| Scavenger population | 0.004985-0.562223 |
| Scavenger after 10 / 20 seconds of plucks | 0.188151 / 0.355726 |
| Measured Bloom-tail resource max | 0.219881 |
| Scavenger raw / routed return RMS | 0.019225 / 0.006251 |
| Scavenger routed peak | 0.073225 |
| Scavenger support max | 0.057225 |
| Bloom trajectory delta with support edge removed | 0.029269 |
| Grazer population | 0.002193-0.687006 |
| Measured Predator pre-Grazer high max | 0.569650 |
| Physical pre/post high reduction max | 0.050228 |
| Grazer branch delta RMS | 0.016044 |
| Predator-food suppression max | 0.280243 |
| Predator trajectory delta with Grazer reduction removed | 0.027823 |
| Dry/processed difference RMS | 0.054843 |
| Non-finite audio / invalid populations | 0 / 0 |

The counterfactual gates are intentionally stricter than nonzero telemetry: disconnecting physical Grazer attenuation must materially change Predator food and population, while removing measured Scavenger return support must materially change Bloom's population trajectory. The accelerated eight-hour bounded-state test remains passing.

## Phase 5 results

Phase 5 inserts Parasite serially after Bloom. Parasite food requires both a viable Bloom population and measured Bloom audio; its DSP intensity vanishes with the host. Host pressure is additionally gated by a measured, gain-matched difference between the Bloom signal before and after Parasite processing.

Latest complete CTest run:

```text
10/10 tests passed
phase2_gain_only_gate       passed
phase3_predator_audio       passed
phase3_bloom_audio          passed
phase4_grazer_audio         passed
phase4_scavenger_audio      passed
phase5_parasite_audio       passed
phase5_host_ecology         passed
phase5_host_audio           passed
phase3_closed_loop_render   passed
phase4_closed_loop_render   passed
```

The deterministic 90-second Phase 5 real-audio host gate produced:

| Metric | Result |
|---|---:|
| Parasite population maximum | 0.633939 |
| Gain-matched Parasite modification ratio | 0.117175 |
| Parasite host-pressure maximum | 0.226824 |
| Bloom trajectory delta with physical modification removed | 0.019340 |
| Parasite at host collapse start / end | 0.089271 / 0.00000008 |
| Parasite after 30 seconds of host recovery | 0.089140 |
| Non-finite samples | 0 |

The same habitat with its measured host inputs removed leaves Parasite effectively extinct. Removing only measured physical modification eliminates its pressure and changes Bloom's trajectory, demonstrating that the relationship is not decorative telemetry. The Parasite DSP also passes exact zero-intensity bypass, bounded output, reset, automation-smoothing, and non-finite-input tests. The accelerated eight-hour Phase 5 model test remains finite and bounded with zero numerical recoveries.
