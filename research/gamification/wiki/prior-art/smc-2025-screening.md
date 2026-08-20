---
title: SMC 2025 title and abstract screening
type: overview
status: draft
updated: 2026-08-20
source_ids: [smc-2025-zenodo-inventory]
tags: [smc, prior-art, screening, coverage]
---

# SMC 2025 title and abstract screening

## Scope and non-claim

The public Zenodo `smc` community returned 59 deposits published in 2025. Every
deposit was verified as a conference paper whose meeting title is `Sound and
Music Computing 2025`. The inventory contains 58 unique PDF payloads because
two deposits point to the same file checksum.

This page reports an LLM title-and-abstract screen. Except for the two papers
already ingested into the wiki, it is not a full-text review and does not treat
abstract claims as independently verified results.

- [Immutable inventory receipt](../../raw/sources/smc-2025-inventory.md)
- [Machine-readable screening](smc-2025-screening.jsonl)

## Routing result

| Route | Deposits | Meaning |
|---|---:|---|
| Direct | 10 | Measures or implements a current JammerNetz concern |
| Enabling | 9 | Supplies a representation, dataset, tool, or evaluation method |
| Design | 9 | May inform feedback, interaction, pedagogy, or guardrails |
| Uncertain | 8 | Needs a cheap introduction/conclusion check |
| Exclude | 22 | No credible current decision or experiment connection |
| Duplicate | 1 | Identical PDF already represented by another deposit |
| **Total** | **59** | Every discovered deposit has one route |

The 19 direct and enabling works form the full-text queue. Two are already
ingested, leaving 17 new full-text reviews before the enabling queue is
expanded.

## Full-text queue: direct

| Work | JammerNetz decision it could change |
|---|---|
| [Real-time piano transcription plus symbolic score following](https://zenodo.org/records/15843588) | Lead-sheet position tracking and recovery |
| [Pitch estimation under ensemble cross-talk](https://zenodo.org/records/15835033) | Pitch confidence, bleed tolerance, and abstention |
| [Repetition-aware segment boundary detection](https://zenodo.org/records/15837937) | Phrase boundaries, recurrence, and four-bar form |
| [Procedural rhythm transformation](https://zenodo.org/records/15838217) | Consistency-versus-variety representation |
| [Wearable synchronization for group music](https://zenodo.org/records/15838480) | Offline alignment and clock-drift correction |
| [Synchronous distributed spatial-audio platform](https://zenodo.org/records/15838463) | Network clock guarantees for time-sensitive audio |
| [User-correctable performance alignment](https://zenodo.org/records/15838699) | Human correction when automatic scoring is wrong |
| [Audio-to-score alignment for onset labels](https://zenodo.org/records/15838731) | Reliable onset-label generation and confidence |
| [Latency-controlled online chord recognition](../sources/smc2025-chang-su-latency-controlled-acr.md) | Causal harmony baseline and latency budget |
| [Performance-MIDI beat and downbeat tracking](../sources/smc2025-murgul-heizmann-midi-beat-downbeat.md) | MIDI-derived beat and bar position |

## Full-text queue: enabling

| Work | Transfer candidate |
|---|---|
| [RASTA real-time spectral toolbox](https://zenodo.org/records/15835163) | C#/Unity low-latency descriptors and reactive visuals |
| [Four decades of the live piece *Jupiter*](https://zenodo.org/records/15837463) | Score-following maintenance and regression testing |
| [Extracting microtonal tuning systems](https://zenodo.org/records/15838081) | Tuning models beyond equal-tempered diatonic labels |
| [Text-conditioned symbolic drumbeat generation](https://zenodo.org/records/15838008) | Novelty measures paired with human judgment |
| [Universal synthesizer dataset generator](https://zenodo.org/records/15838160) | Controlled MIDI/audio patch-attack corpora |
| [Music Boomerang audio augmentation](https://zenodo.org/records/15838181) | Rhythm-preserving augmentation for scarce labels |
| [Scheme for Max sample-level processing](https://zenodo.org/records/15838249) | Rapid causal feature prototyping |
| [Soundsketcher crossmodal visualization](https://zenodo.org/records/15838270) | Perceptually motivated audio-to-visual mappings |
| [Orchestral blend detection](https://zenodo.org/records/15839409) | Synchrony, harmonicity, parallelism, and role relations |

## Design watchlist

- [Flow Swing](https://zenodo.org/records/15843562): avoid treating expressive
  non-isochronous timing as error.
- [Electro/acoustic Soundpainting recognition](https://zenodo.org/records/15843324):
  explicit leadership and ensemble-control vocabulary.
- [Music interpretation and emotion perception](https://zenodo.org/records/15837429):
  evidence against reducing improvisation to mechanical correctness.
- [The Strange Pulse Toolkit](https://zenodo.org/records/15837615): preserve
  exploration, unpredictability, and ceded control.
- [Audio-visual roughness](https://zenodo.org/records/15838308): perceptually
  grounded colors and shapes rather than arbitrary warning mappings.
- [Belly of the Beast](https://zenodo.org/records/15838382): guided exploration
  versus creative freedom in interactive-music UX.
- [Speech-based aligned annotations](https://zenodo.org/records/15839750):
  possible eyes-free measure, chord, and structure feedback.
- [Kinesthesis 2.0 with Somax2](https://zenodo.org/records/15839788): embodied
  human-agent improvisation and response.
- [Spatial awareness for co-creative agents](https://zenodo.org/records/15840076):
  mutual influence and role-aware interaction.

## Uncertain queue

These receive only a short introduction/conclusion check unless that reveals a
specific method, metric, or human result:

- behavioral effects of harmonic and rhythmic feedback in e-car driving;
- vibrotactile musical feedback for children with hearing impairment;
- composer adaptation using a portable cochlear-implant simulator;
- concept-based explanations for music-emotion recognition;
- the GuqinSonGest articulation and timbre dataset;
- physioactive listening;
- participatory AI composition of a school song;
- human-centred paper prototyping for synthesizer interfaces.

## Exclusion audit

To catch attractive-sounding false rejections, the exclusion set was sorted by
the SHA-256 of `smc-2025-exclusion-audit-v1:<zenodo-id>` and the first three
records were reconsidered. This samples 3 of 22 exclusions, exceeding ten
percent.

| Record | Advocate check | Result |
|---|---|---|
| [BEE-MER](https://zenodo.org/records/15837365) | Could embeddings transfer to performance features? | Exclusion upheld: no specific current measurement or evaluation transfer |
| [Harmony in Complexity](https://zenodo.org/records/15839861) | Could audience participation inform interaction? | Exclusion upheld: participation is in climate-data sonification, not ensemble behavior |
| [Pitch/tempo change and preference](https://zenodo.org/records/15837314) | Could preference results constrain feedback? | Exclusion upheld: global recording transformations do not test player feedback |

The audit is a workflow check, not a statistical recall guarantee. A human
reviewer has not yet approved the exclusions.

## Duplicate-deposit finding

Records `15843588` and `15838794` have the same title, PDF filename, byte size,
and MD5 checksum, but their Zenodo creator metadata differs. Record `15843588`
is the working canonical record; the other remains in the raw inventory and is
routed as `duplicate` until the source audit is complete.

## Next operation

Read the eight not-yet-ingested direct works first. For each, audit datasets,
metrics, causal look-ahead, code, model, and license availability before
creating a maintained source page. The nine enabling works follow only where
their transfer candidate still closes a concrete experiment gap.
