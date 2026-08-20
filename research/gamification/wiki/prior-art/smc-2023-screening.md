---
title: SMC 2023 title and abstract screening
type: overview
status: draft
updated: 2026-08-20
source_ids: [smc-2023-zenodo-inventory]
tags: [smc, prior-art, screening, coverage]
---

# SMC 2023 title and abstract screening

## Scope

The verified Zenodo inventory contains 61 conference papers and one complete
proceedings book. Every paper received an LLM title-and-abstract route. This is
a high-recall screen, not a full-text review or human-approved exclusion set.

- [Immutable inventory receipt](../../raw/sources/smc-2023-inventory.md)
- [Machine-readable screening](smc-2023-screening.jsonl)

## Routing result

| Route | Records |
|---|---:|
| Direct | 11 |
| Enabling | 6 |
| Design | 11 |
| Uncertain | 10 |
| Exclude | 23 |
| Proceedings book | 1 |
| **Total** | **62** |

## First full-text wave: direct

- [CREPE Notes pitch-contour segmentation](https://zenodo.org/records/8315899)
- [Real-time dynamical-complexity measurement](https://zenodo.org/records/8334846)
- [Score-informed note-level MIDI velocity estimation](https://zenodo.org/records/8341233)
- [Audio-to-score synchronization with iterative refinement](https://zenodo.org/records/8341257)
- [Network client latency and synchrony](https://zenodo.org/records/8343643)
- [Real-time cent-sensitive tuning visualization](https://zenodo.org/records/8398885)
- [Collective Rhythms group-synchrony visualization](https://zenodo.org/records/8398927)
- [Onset-based legato transcription](https://zenodo.org/records/8398933)
- [Melodic-boundary detection](https://zenodo.org/records/8398985)
- [Embodied tempo tracking](https://zenodo.org/records/10060971)
- [Live score following for synchronized visual accompaniment](https://zenodo.org/records/10062934)

## Second full-text wave: enabling

- [Ghanaian singing F0 analysis](https://zenodo.org/records/8316086): evidence
  against assuming equal temperament is culturally neutral.
- [Bass-trombone legato transitions](https://zenodo.org/records/8334783):
  transition-duration and energy-stability articulation descriptors.
- [WebChucK IDE](https://zenodo.org/records/8334822): accessible browser-based
  audio and visualization prototyping.
- [MIDI 2.0 monitor](https://zenodo.org/records/8341243): event observability and
  diagnostics.
- [Coupled-oscillator complex rhythms](https://zenodo.org/records/8398973):
  representation between periodicity and chaos.
- [musif](https://zenodo.org/records/13359873): extensible symbolic-music
  feature extraction.

## Design and uncertainty

The design route retains minimal rhythmic controls, vibrotactile feedback,
accessible interfaces, human-AI accompaniment, movement sonification, a
musical training game, interactive aural analysis, meaningful minimal input,
music-to-color mappings, visuospatial pattern tools, and audiotactile
performance. Ten uncertain papers remain available for cheap
introduction/conclusion checks rather than immediate full reading.

## Inventory anomaly

Zenodo record `8315899` contains six PDF files: its own paper plus five payloads
that also occur in their respective paper deposits. Consequently the 61 paper
deposits contain 66 PDF references but still exactly 61 unique PDF payloads.
The raw inventory preserves the anomaly rather than silently rewriting it.

## Exclusion audit

The exclusion set was sorted by SHA-256 of
`smc-2023-exclusion-audit-v1:<zenodo-id>` and the first three records were
reconsidered. All three exclusions were upheld:

- [Symbolic music generation with diffusion models](https://zenodo.org/records/8407166)
- [Music-credits network analysis](https://zenodo.org/records/10061131)
- [Computational piano-model analysis](https://zenodo.org/records/10061286)

This deterministic audit samples ten percent of exclusions after rounding up;
it is not a statistical recall guarantee.
