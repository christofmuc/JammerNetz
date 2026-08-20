---
title: JammerNetz gamification research overview
type: overview
status: seed
updated: 2026-08-20
tags: [gamification, musicality, research]
---

# JammerNetz gamification research overview

## Working thesis

JammerNetz should not imitate a score-following game that rewards reproduction
of prescribed notes. It should recognize evidence that musicians are making
repeatable choices related to a shared pulse, form, harmony, and one another.
The game rewards musical relationship rather than virtuosity or continuous
activity.

The initial product hypothesis is:

> Confidence-weighted feedback about pulse, recognizable intent, listening,
> recurrence, development, and recovery can help people learn to jam together
> while making sessions more playful.

## Research model

```text
musical behavior we value
        -> observable audio/MIDI evidence
        -> confidence-aware measurement
        -> game mechanic and feedback
        -> recorded-session evaluation
        -> human-reviewed product decision
```

No arrow is automatic. A measurement can be technically accurate but teach the
wrong behavior; a delightful mechanic may depend on evidence we cannot measure
reliably; a successful offline experiment may still violate real-time latency
or CPU constraints.

## Current foundations

- Each participant already supplies identifiable channels, avoiding general
  source separation.
- JammerNetz has a server sample timeline and BPM/MIDI-clock transport.
- Planned performance MIDI can represent symbolic intent for synthesizers and
  drum machines.
- Audio remains authoritative for perceptual onset, dynamics, articulation,
  patch envelopes, effects, and electric guitar.
- Historical session recordings can support cheap offline experiments before
  any production UI or protocol is committed.

## Sections

1. [Musicality](musicality/overview.md) defines what the system should encourage.
2. [Measurements](measurements/overview.md) defines what can actually be observed.
3. [Game design](game-design/overview.md) turns evidence into feedback and play.
4. [Architecture](architecture/overview.md) separates research from production.
5. [Prior art](prior-art/overview.md) maps the literature and reusable artifacts.
6. [Experiments](experiments/overview.md) tests hypotheses against recordings.
7. [Decisions](decisions/overview.md) records human-approved conclusions.

## Guardrails

- Silence and restraint can be valuable participation.
- A stable ahead/behind pocket is not the same as unstable timing.
- Network arrival time is not musician timing.
- MIDI describes control intent, not necessarily audible output.
- Diatonic membership is not a complete model of harmonic quality.
- Scores degrade gracefully to `unknown`; uncertainty is visible.
- Cooperative progress is primary; public shaming and permanent leaderboards
  are out of scope until evidence says otherwise.
