---
title: SMC 2024 title and abstract screening
type: overview
status: draft
updated: 2026-08-20
source_ids: [smc-2024-zenodo-inventory]
tags: [smc, prior-art, screening, coverage]
---

# SMC 2024 title and abstract screening

## Scope

The verified Zenodo inventory contains 77 conference papers and one complete
proceedings book. Every paper received an LLM title-and-abstract route. This is
a high-recall screen, not a full-text review or human-approved exclusion set.

- [Immutable inventory receipt](../../raw/sources/smc-2024-inventory.md)
- [Machine-readable screening](smc-2024-screening.jsonl)

## Routing result

| Route | Records |
|---|---:|
| Direct | 13 |
| Enabling | 11 |
| Design | 7 |
| Uncertain | 6 |
| Exclude | 40 |
| Proceedings book | 1 |
| **Total** | **78** |

## First full-text wave: direct

- [Real-time flute-technique recognition for co-creative agents](https://zenodo.org/records/14334950)
- [Distributed performance mediated by a web co-creative agent](https://zenodo.org/records/14335001)
- [Piano accompaniment evaluated against human ensemble timing](https://zenodo.org/records/14335624)
- [Real-time future-rhythm visualization with delay calibration](https://zenodo.org/records/14335732)
- [Score-dependent synchronization in piano duet](https://zenodo.org/records/14337192)
- [Simulating performance mistakes for music learning](https://zenodo.org/records/14337238)
- [Quantifying pitch drift for performer self-assessment](https://zenodo.org/records/14337696)
- [Human and algorithmic musical-boundary annotation](https://zenodo.org/records/14337734)
- [Beat induction from a swarm of uncertain onsets](https://zenodo.org/records/14338139)
- [Noise-robust multimodal score following](https://zenodo.org/records/14339207)
- [Multi-level tempo estimation](https://zenodo.org/records/14362633)
- [ChordSync chord-to-audio alignment](https://zenodo.org/records/14362873)
- [Segmentation of live concert audio](https://zenodo.org/records/14363076)

## Second full-text wave: enabling

- [Patterns UI](https://zenodo.org/records/14335898): melodic-pattern search and
  visualization.
- [Guitar chord-diagram suggestion](https://zenodo.org/records/14336096):
  contextual voicing and texture consistency.
- [Audio-controllable MIDI arpeggiator](https://zenodo.org/records/14336426):
  pitch-estimation accuracy and delay in a playable system.
- [Composition-aware loop recommendation](https://zenodo.org/records/14336568):
  complementary role selection.
- [Perde-space](https://zenodo.org/records/14337758): expert-correlated harmonic
  distance beyond Western equal temperament.
- [Module-level MIDI](https://zenodo.org/records/14361429): fine-grained MIDI
  routing conventions.
- [libremidi](https://zenodo.org/records/14361546): allocation-conscious,
  cross-platform real-time MIDI 1/2 transport.
- [Real-time masking compensation](https://zenodo.org/records/14361929):
  psychoacoustic evidence for overlapping participant spectra.
- [Audio-effects dataset generator](https://zenodo.org/records/14362474):
  sample-accurate effect and patch calibration corpora.
- [Charlie Parker transcription pipeline](https://zenodo.org/records/14362769):
  aligned score/audio/downbeat benchmark and modular transcription.
- [Tresillo rhythm prevalence](https://zenodo.org/records/14362917): formalized
  rhythmic-pattern similarity measures.

## Design and uncertainty

The design route retains work on embodied performance, explainable musical
models, game-based timbre training, accessible collaboration, expressive
performance analysis, human disagreement with objective audio metrics, and
accessible-instrument evaluation. Six uncertain records cover accessible music
preferences, guitar articulation generation, lead-sheet-to-tablature systems,
Makam visualization, MIDI protocol extensions, and biosensor performance.

## Exclusion audit

The exclusion set was sorted by SHA-256 of
`smc-2024-exclusion-audit-v1:<zenodo-id>` and the first four records were
reconsidered. All four exclusions were upheld:

- [Beamix directional Ambisonics effects](https://zenodo.org/records/14338799)
- [Continuously variable delay-line implementation](https://zenodo.org/records/14361885)
- [*Loom* oral tradition in immersive media](https://zenodo.org/records/14362205)
- [Speculative machine learning in sound synthesis](https://zenodo.org/records/14362528)

This deterministic audit samples ten percent of exclusions after rounding up;
it is not a statistical recall guarantee.
