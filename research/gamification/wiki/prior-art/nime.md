---
title: NIME proceedings discovery source
type: overview
status: seed
updated: 2026-08-21
tags: [nime, prior-art, interaction-design, collaboration]
source_ids: [nime-paper-proceedings-index, nime-bibliography]
---

# NIME proceedings discovery source

## Why NIME belongs in this project

NIME is complementary to Sound and Music Computing rather than a replacement
for it. SMC and ISMIR are more likely to provide the core onset, beat, chord,
and structure-analysis methods. NIME is more likely to show how measurements
are placed inside playable instruments and interactive systems, how feedback
changes performer behavior, and how collaborative music technology is
evaluated with people.

For JammerNetz, this makes NIME primarily a source for answering:

> What should the system show, reward, or change so that musicians listen and
> adapt to one another without turning the jam into a correctness test?

The official [paper proceedings index](https://www.nime.org/papers/) covers the
peer-reviewed paper track. Its public
[bibliography repository](https://github.com/NIME-conference/NIME-bibliography)
provides titles, abstracts, keywords, identifiers, and links in structured
formats suitable for a later metadata screen. See the immutable
[source receipt](../../raw/sources/nime-proceedings.md).

## What we might find

### Collaborative musical interaction

- systems designed for ensemble improvisation rather than solo accuracy;
- shared control, interdependent instruments, and changing performer roles;
- call-and-response, leadership, mutual adaptation, and distributed agency;
- networked or co-located performance systems and collective instruments;
- ways to make each contribution audible without forcing everyone into the
  same pattern.

This literature may help us define positive ensemble behaviors before trying
to infer them from audio and MIDI.

### Feedback and game interaction

- visual histories of recent musical activity and previews of upcoming state;
- peripheral, haptic, vibrotactile, spatial, or sonic feedback;
- playful constraints, cooperative challenges, and interfaces that treat
  failure as exploration;
- techniques for preventing feedback from monopolizing visual attention;
- mappings from measurements to cues that remain understandable during live
  performance.

The useful result may be an interaction pattern or evaluation method rather
than a reusable algorithm.

### Learning, access, and participation

- low-floor, high-ceiling instruments for novices and mixed-skill groups;
- scaffolding that enables participation without reducing all music to a
  prescribed score;
- music-learning interfaces, practice feedback, and group pedagogy;
- accessibility work that exposes assumptions in screen-heavy, timing-heavy,
  or notation-heavy designs;
- studies of agency, confidence, engagement, and willingness to continue.

This is directly relevant to the goal of rewarding meaningful participation
rather than reproducing a Guitar Hero note-matching task.

### Evaluation of ensemble experience

- observational and self-report measures of coordination, awareness,
  interactivity, agency, engagement, and workload;
- study designs comparing different feedback modalities;
- qualitative methods for determining why an interface helped or distracted;
- measures of whether participants listened and reacted to one another;
- evaluation dimensions for collaborative musical systems.

These methods may help establish human ground truth for whether a JammerNetz
score is musically fair. They do not by themselves validate an automatic audio
metric.

### Enabling technical work

- real-time gesture and instrumental-technique recognition;
- guitar, percussion, controller, and sensor datasets;
- interactive machine learning and performer-specific adaptation;
- low-latency mappings, distributed control, and network-music systems;
- score following, beat tracking, or onset detection embedded in a playable
  prototype.

NIME papers may demonstrate such components without benchmarking them as
thoroughly as a MIR venue. Strong technical candidates should therefore be
followed into their datasets, implementations, citations, and later work.

## Expected low-yield areas

Much of NIME is intentionally outside the JammerNetz research question:

- construction of a standalone instrument without ensemble, sensing,
  feedback, or learning implications;
- synthesis or mapping techniques that do not observe musical interaction;
- installations and artistic reports without a transferable design claim or
  evaluation;
- environmental sonification, archival work, or cultural analysis unrelated
  to collaborative performance;
- interfaces that require replacing the participants' existing instruments.

These works can be valuable on their own terms while still being excluded from
this bounded search.

## Screening cautions

Simple keywords will be noisy. `Feedback` often means acoustic feedback rather
than information returned to a musician. `Network` may mean a neural network
rather than networked performance. Conversely, relevant work may use terms
such as `agency`, `interdependence`, `entrainment`, `empathy`, or `social
interaction` without mentioning a jam or ensemble in the title.

A future screen should therefore classify titles, abstracts, and keywords
against explicit research cards:

1. collaborative improvisation and ensemble coordination;
2. musician-facing feedback and cooperative game design;
3. learning, accessibility, and mixed-skill participation;
4. real-time sensing relevant to guitar, rhythm, or musical gestures;
5. network latency, clocks, and distributed musical control;
6. evaluation methods for musical interaction and fairness.

Use the existing `direct`, `enabling`, `design`, `uncertain`, and `exclude`
routes, but interpret `design` more generously than in the SMC screen because
interaction design is the main reason to search NIME.

## Proposed first operation

Run a metadata-only pilot over one recent complete year from the bibliography
export. Record route counts and reconsider a deterministic exclusion sample.
Do not download or ingest individual papers until the pilot establishes useful
precision. If the pilot works, expand historically because relevant ensemble
and feedback systems may predate current terminology.

The Music and Installation proceedings are out of scope for the first pass.
They can be added later if the paper track points to a specific practice or
artifact that cannot be understood from its paper alone.
