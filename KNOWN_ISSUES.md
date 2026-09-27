# Known issues

## Phase 6 audio integration

On 2026-09-27, the core Release build succeeded with MSVC 19.51 and Ninja.
CTest passed 12/13 checks. `phase6_repetition_audio` failed expectations for:

- Decomposer growth on repeated material, starvation on fresh material and recovery.
- Gain-matched audible modification and consumption of old material.
- Transient fragment production and effects on Predator, Scavenger and Bloom.

This source snapshot preserves the current development state. It is not a
validated Phase 6 release. The source publication did not change DSP behaviour.
No new end-to-end DAW validation was performed during publication.
