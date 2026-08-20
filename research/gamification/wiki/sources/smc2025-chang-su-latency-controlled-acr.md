---
title: Chang and Su 2025 - latency-controlled online chord recognition
type: source
status: reviewed
updated: 2026-08-20
source_ids: [SMC2025-CHANG-SU-LATENCY-CONTROLLED-ACR]
tags: [smc, chord-recognition, transformer, causal, latency]
---

# Chang and Su 2025

## What the source reports

The model performs online major/minor/no-chord recognition using a contextual
block processing Transformer. It uses 22.05 kHz audio, a six-octave CQT with a
93 ms hop, inherited context between blocks, limited look-ahead, and a
Tonnetz-derived chord-distance target. The model has 7.54 million trainable
parameters.

At 186 ms reported algorithmic latency, aggregate root, major/minor, and
segmentation scores are 82.91, 80.26, and 82.83. The corresponding offline
baseline scores are 83.72, 81.36, and 83.67. The paper notes that root evidence
often appears earlier than the third needed to distinguish major from minor.

## Interpretation

This is strong `external-empirical` evidence that limited-look-ahead harmony
feedback is feasible. Reported latency excludes complete device, windowing,
inference, queue, and UI latency. Evaluation uses commercial music mixes and a
small chord vocabulary, not isolated live guitar.

## JammerNetz relevance

Disposition: `adapt`. The contextual block mechanism, staged root/type display,
explicit no-chord class, and musical-distance loss all match JammerNetz needs.
The Tonnetz loss is also a promising basis for partial-credit game feedback.

## Open questions

- Are implementation and trained weights available from the authors?
- What is complete CPU inference latency on supported client hardware?
- Does isolated JammerNetz guitar allow a smaller model or wider vocabulary?
- How should lead-sheet and synth-MIDI priors enter the model?

## Source

- [Raw receipt](../../raw/sources/smc2025-chang-su-latency-controlled-acr.md)
- [Zenodo record](https://zenodo.org/records/15838757)
