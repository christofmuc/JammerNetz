---
title: Gamification research queue
type: overview
status: seed
updated: 2026-08-20
tags: [research-queue]
---

# Gamification research queue

Questions are promoted here only when answering them could change an
experiment, architecture, or product decision.

## Priority: establish the first playable experiment

1. Which causal onset detector and confidence model works across clean guitar,
   distorted guitar, drums, plucks, pads, and legato synth patches?
2. How should local perceived-clock timing and canonical ensemble timing be
   calibrated and reported separately?
3. Which rhythm-only bar descriptor best separates recurrence, development,
   and unrelated noise on historical sessions?
4. What cooperative visualization remains useful without distracting players?
5. What minimum human-labeling protocol is needed to judge whether feedback is
   musically fair rather than merely numerically stable?

## Priority: harmony

1. Can a causal major/minor/no-chord model reproduce the 2025 CBP results on
   isolated JammerNetz guitar with lower complexity?
2. Does lead-sheet or synth-MIDI conditioning outperform unconditional chord
   recognition for guitar compatibility scoring?
3. Which pitch-class, tension-resolution, and chord-distance measures avoid
   penalizing blues, suspensions, approaches, and deliberate chromaticism?

## Priority: prior art

1. Complete a metadata-level SMC proceedings inventory.
2. Screen it for onset, beat, harmony, score following, networked performance,
   co-creativity, education, and musical HCI.
3. Follow the strongest citations into ISMIR, NIME, ICMC, DAFx, AES, and
   ICASSP.
4. Audit implementation, model, dataset, and license availability separately
   from paper relevance.

## Later: autoresearch candidates

- Maximize guitar chord-compatibility accuracy under a fixed causal latency and
  CPU budget.
- Optimize onset timing error and false-event rate across instrument strata.
- Search motif descriptor weights against held-out human judgments.

These become autoresearch tasks only after corpus splits, metrics, compute
budgets, and automatic accept/reject criteria are frozen.
