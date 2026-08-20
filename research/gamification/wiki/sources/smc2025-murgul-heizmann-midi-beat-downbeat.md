---
title: Murgul and Heizmann 2025 - beat and downbeat tracking from performance MIDI
type: source
status: reviewed
updated: 2026-08-20
source_ids: [SMC2025-MURGUL-HEIZMANN-MIDI-BEAT-DOWNBEAT]
tags: [smc, midi, beat, downbeat, transformer]
---

# Murgul and Heizmann 2025

## What the source reports

The system tokenizes performance MIDI and uses a reduced T5 encoder-decoder to
generate beat and downbeat time tokens. Inputs include note onsets, offsets,
and optionally velocity. Training uses overlapping 10-second segments;
inference is autoregressive with five-beam search.

Reported beat/downbeat F1 is 98.01/76.56 on A-MAPS, 78.13/27.81 on ASAP,
52.38/23.02 on GuitarSet, and 57.72/29.75 on Leduc. Velocity helps downbeat
tracking. The paper also documents substantial sensitivity to annotation
alignment and a 70 ms evaluation tolerance.

## Interpretation

This is not a low-latency master-clock implementation. It is useful evidence
for recovering metrical structure from symbolic performance or for post-session
analysis. Explicit transport remains preferable when available. The weaker
guitar results and stronger audio-beat baselines support hybrid audio/MIDI
evidence.

## JammerNetz relevance

Disposition: `reference`, with possible later `adapt` use for clockless session
recovery, meter inference, or validation of uploaded MIDI. The public repository
was still marked under construction when inspected.

## Open questions

- Can a causal non-autoregressive model infer bar position from multi-participant
  MIDI with a short window?
- Does combining drum audio accents with synth MIDI improve downbeats?
- How should inferred metrical structure coexist with an explicit clock?

## Source

- [Raw receipt](../../raw/sources/smc2025-murgul-heizmann-midi-beat-downbeat.md)
- [Zenodo record](https://zenodo.org/records/15838779)
- [Associated repository](https://github.com/Klangio/midi-beat-tracking)
