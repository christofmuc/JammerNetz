---
title: Measurement families
type: measurement
status: seed
updated: 2026-08-20
tags: [audio, midi, measurement]
---

# Measurement families

Measurements provide evidence for musicality dimensions. They do not define
musicality by themselves.

## Authoritative or high-confidence inputs

- Shared transport: tempo map, beat phase, bar index, meter, and section.
- Performance MIDI: ordered note, velocity, controller, bend, and sustain
  events in the audio sample timeline.
- Lead sheet: chord and section events attached to transport positions.
- Participant and channel identity.

## Audio observations

- Perceptual onset time, strength, frequency-band distribution, and confidence.
- Activity, silence, loudness, dynamic contour, and clipping.
- Predominant pitch and pitch trajectory for monophonic material.
- Chroma or harmonic pitch-class evidence for polyphonic guitar.
- Spectral occupancy, masking, timbre, and articulation.

## Derived temporal evidence

- Offset from plausible subdivisions.
- Personal phase bias and residual timing variance.
- Drift, recovery, accent maps, density, and phrase-boundary behavior.
- Local perceived-clock timing versus canonical ensemble timing.

## Derived structural evidence

- Bar-level onset/accent descriptors.
- Self-similarity, motif recurrence, variation distance, and call-and-response.
- Turn-taking, overlap, space, and dynamic adaptation.
- Chord compatibility, harmonic distance, tension, and resolution.

## Required confidence model

Every observation needs validity, confidence, time support, and failure reason.
Missing evidence is not negative evidence. Pads, legato notes, effects tails,
distorted guitar, arpeggios, speech, and packet discontinuities require
different abstention policies.

## Measurement contract

A measurement page should state input channels, sample rate, window and hop,
causal look-ahead, expected latency, instrument assumptions, calibration,
output units, confidence definition, known confounders, and benchmark status.
