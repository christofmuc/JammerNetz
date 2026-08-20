---
title: Nadar, Abesser, and Grollmisch 2019 - seventh-chord CNN
type: source
status: reviewed
updated: 2026-08-20
source_ids: [SMC2019-NADAR-ABESSER-GROLLMISCH-SEVENTH-CHORDS]
tags: [smc, chord-recognition, cnn, guitar]
---

# Nadar, Abesser, and Grollmisch 2019

## What the source reports

The method classifies chord root and type jointly or separately from
log-frequency spectral patches. Its extended vocabulary contains power, major,
minor, major seventh, minor seventh, dominant seventh, and half-diminished
seventh across twelve roots.

The frontend uses 44.1 kHz audio, an 8,192-sample STFT, 100 ms feature hops,
and 1.5 seconds of context with predictions every 400 ms. Reported F-score is
0.97 for the 84-class controlled isolated-chord split and about 0.66 on mixed
popular music. Major/minor recognition on isolated polyphonic guitar reaches
about 0.90-0.91; extended-vocabulary performance on that real-guitar set is not
reported.

## Interpretation

The controlled result shows feasibility but is sensitive to timbre and voicing
coverage. The context and output hop fit offline annotation better than immediate
feedback. The associated synthetic dataset is valuable for baselines but does
not substitute for real JammerNetz guitar.

## JammerNetz relevance

Disposition: `adapt`. Reuse the separated root/type idea and log-frequency
frontend as baselines. Evaluate confidence-weighted pitch-class evidence and
lead-sheet conditioning before training an unconditional 84-class recognizer.

## Open questions

- Are dataset and future weight licenses compatible with JammerNetz distribution?
- How much real distorted-guitar data is required for generalization?
- Can a causal, smaller model perform better when conditioned on transport,
  synth MIDI, or a lead sheet?

## Source

- [Raw receipt](../../raw/sources/smc2019-nadar-abesser-grollmisch-seventh-chords.md)
- [Conference paper](https://smc2019.uma.es/articles/S8/S8_05_SMC2019_paper.pdf)
