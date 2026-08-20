# Gamification LLM wiki schema

These instructions apply to `research/gamification/` and its descendants.
They instantiate the LLM Wiki pattern for the JammerNetz gamification research
project.

## Purpose and boundary

The wiki accumulates source-grounded knowledge and experiment results before
ideas enter production. Do not modify JammerNetz production code as part of a
wiki ingest, query, lint, or research operation. Productization requires a
reviewed decision page and a separately scoped implementation task.

## Layers

1. `raw/` contains immutable source receipts. After a receipt is added, never
   rewrite it to match a later interpretation. Add a new receipt or correction.
2. `wiki/` is the maintained synthesis. Agents may create, refactor, and
   cross-link pages as evidence develops.
3. This file is the schema. Update it deliberately when the workflow changes.

Large or licensed binaries remain outside Git. A source receipt records a
stable URL or DOI, bibliographic identity, retrieval date, and local checksum
when available. Never encode a machine-specific absolute corpus path.

## Wiki sections

- `musicality/`: the musical behaviors the project wants to encourage.
- `measurements/`: observable audio, MIDI, transport, and derived evidence.
- `game-design/`: scoring, feedback, quests, fairness, and anti-gaming.
- `architecture/`: research pipeline and possible future system boundaries.
- `prior-art/`: venue coverage and cross-source comparisons.
- `experiments/`: hypotheses, benchmark contracts, and reproduced results.
- `decisions/`: evidence-backed, human-reviewed conclusions.
- `sources/`: one maintained summary per ingested source.

## Page metadata

Normal wiki pages begin with YAML frontmatter containing:

```yaml
title: Human-readable title
type: overview | concept | measurement | mechanic | source | experiment | decision
status: seed | draft | reviewed | validated | superseded
updated: YYYY-MM-DD
tags: [short, stable, tags]
```

Source-backed pages also list `source_ids`. Experiment pages list the corpus,
split unit, code revision, and metric definitions. Decision pages list their
owner and decision status.

## Evidence discipline

Keep these statements visibly separate:

1. what a source explicitly reports;
2. our interpretation;
3. the implication for JammerNetz;
4. unknowns that require an experiment.

Record sample sizes, datasets, evaluation tolerances, causal look-ahead,
latency definitions, instrument conditions, code/model availability, and
licenses. Do not write "real time," "accurate," or "state of the art" without
the qualifying numbers and date. Do not convert MIDI/audio agreement or a
machine prediction into human ground truth.

Use these evidence levels where a conclusion needs a compact label:

- `JN-validated`: reproduced on held-out JammerNetz sessions.
- `external-empirical`: supported by an external evaluation.
- `method-proposal`: technically described but not reproduced here.
- `design-hypothesis`: an idea awaiting evidence.

## Operations

### Ingest

1. Add one immutable source receipt under `raw/sources/`.
2. Read the complete relevant source, including tables, figures, limitations,
   footnotes, and supplementary implementation links.
3. Create or update its page under `wiki/sources/`.
4. Update affected concept, measurement, prior-art, or experiment pages.
5. Update `wiki/index.md` and append a dated entry to `wiki/log.md`.

### Query

Read `wiki/index.md` first, then the smallest set of relevant pages. Cite wiki
pages and ultimately their source receipts. File a query result back into the
wiki only when it contains durable synthesis, not ordinary conversation.

### Research

Start with a bounded question from `wiki/research-queue.md`. Search synonyms,
neighboring venues, prior and subsequent citations, implementations, datasets,
and negative evidence. A proceedings sweep must maintain explicit coverage
counts so "searched" is distinguishable from "read in full." Integrate selected
sources one at a time even when discovery was parallel.

### Experiment

An experiment begins with a falsifiable hypothesis and fixed benchmark
contract. Split recordings by session or date, never by adjacent windows.
Store predictions separately from human labels. Record failures and negative
results. Autoresearch is appropriate only after the objective, time/compute
budget, dataset split, and acceptance metric are executable and fixed.

### Decision

Agents may draft and update evidence for a decision. A human confirms its
status. A decision must state what is being adopted, deferred, or rejected;
why; the supporting evidence; risks; and the condition that would reopen it.

### Lint

Check for broken links, orphan pages, missing source receipts, unsupported
claims, conflicting terminology, stale decision evidence, duplicated concepts,
and research questions that have already been answered. Log every lint pass.

## Index and log

`wiki/index.md` is the content catalog and must be updated whenever pages are
added, renamed, or materially repurposed. `wiki/log.md` is append-only and uses
headings of the form:

```text
## [YYYY-MM-DD] ingest | Source title
## [YYYY-MM-DD] research | Question
## [YYYY-MM-DD] experiment | Experiment ID
## [YYYY-MM-DD] decision | Decision title
## [YYYY-MM-DD] lint | Scope
```
