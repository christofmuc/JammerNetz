---
title: Experiment program
type: overview
status: seed
updated: 2026-08-20
tags: [experiments, evaluation, autoresearch]
---

# Experiment program

## Experiment stages

1. `probe`: establish whether a signal exists in historical recordings.
2. `benchmark`: compare fixed methods on a frozen held-out corpus.
3. `replay`: render candidate feedback against a recorded session timeline.
4. `musician review`: collect judgments about correctness and usefulness.
5. `live pilot`: test distraction, latency, learning, and group behavior.

## Required contract

Every experiment records:

- falsifiable hypothesis;
- corpus manifest and immutable source coordinates;
- session-level train/validation/test split;
- human labels and predictions in separate files;
- baseline and negative controls;
- metric, tolerance, confidence, and abstention definitions;
- code revision, configuration, random seed, and tool versions;
- expected failure modes and stopping rule;
- result, including negative findings.

## First candidate experiment

Replay historical sessions against an explicit beat grid, extract per-channel
audio onsets, and compare several timing representations:

- raw nearest-subdivision offset;
- offset after learned personal phase bias;
- per-bar residual variance;
- drift and recovery events;
- rhythm-only bar self-similarity.

The output is a reviewable timeline, not a player score. Musicians first judge
whether the detected events and explanations are credible.

## Autoresearch readiness

Autoresearch begins only when one executable command performs a fixed-budget
experiment and prints an automatic acceptance metric. The agent may alter only
declared implementation files; corpus splits, labels, time budget, and metric
code remain immutable. All attempts, including failures, are appended to a
machine-readable ledger.
