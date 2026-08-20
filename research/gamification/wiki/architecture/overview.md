---
title: Research and system architecture
type: concept
status: seed
updated: 2026-08-20
tags: [architecture, real-time, corpus]
---

# Research and system architecture

## Research path

```text
immutable recordings and MIDI
        -> reproducible segmentation and alignment
        -> feature/event extraction
        -> predictions with confidence
        -> human review and held-out metrics
        -> game simulation or replay
        -> reviewed product decision
```

Historical corpus rules are documented in
[session-recording-data-mining.md](../../../../docs/session-recording-data-mining.md).

## Hybrid event model

- MIDI represents symbolic control intent for synths and drum machines.
- Audio supplies perceptual onset, realized dynamics, articulation, effects,
  and evidence for guitar.
- Near-simultaneous MIDI notes form one gesture for audio-onset association.
- Player timing and audible landing remain separately measurable.
- Transport events use sample positions, not packet arrival or wall-clock time.

## Future real-time boundary

Audio callbacks may copy bounded frames and publish counters only. Analysis,
inference, scoring, persistence, and UI synthesis run on workers. Queue overflow
drops analysis input and remains observable; it never stalls audio.

Research code may initially run offline. A decision to productize must define:

- causal window and complete end-to-end latency;
- CPU, memory, and model-size budget;
- supported roles and instruments;
- confidence and abstention behavior;
- protocol and compatibility implications;
- deterministic tests and corpus acceptance thresholds.
