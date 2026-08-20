# JammerNetz gamification research

This directory is the research workspace for turning a JammerNetz session into
a cooperative musical game. It is deliberately separated from production
client, server, and plug-in code while ideas are explored against recordings.

The research question is broader than "can we classify notes?":

> Can JammerNetz recognize evidence that participants are sharing pulse, form,
> harmony, motifs, dynamics, and attention, then turn that evidence into fair
> and enjoyable feedback?

## Start here

- [Wiki overview](wiki/overview.md) summarizes the current thesis.
- [Wiki index](wiki/index.md) catalogs every maintained page.
- [Research queue](wiki/research-queue.md) contains bounded questions rather
  than an unstructured idea backlog.
- [Maintenance schema](AGENTS.md) defines ingestion, research, experiment, and
  decision workflows.
- [Raw sources](raw/README.md) contain immutable source receipts. Large PDFs,
  recordings, derived audio, and model files remain outside Git.
- [Research tools](tools/README.md) contain small reproducible harvest and audit
  utilities.

## Research boundary

Work here may include literature reviews, dataset manifests, labels, offline
analysis, benchmark definitions, notebooks, small probes, and experiment
reports. Moving a method into the JammerNetz real-time engine requires a
reviewed decision in the wiki and a separate production implementation task.

The existing corpus preparation work remains authoritative for historical
recordings:

- [Mining historical session recordings](../../docs/session-recording-data-mining.md)
- [Audio/MIDI harmony corpus experiment](../../docs/harmony-audio-midi-corpus-experiment.md)
- [Performance MIDI implementation plan](../../docs/performance-midi-implementation-plan.md)

## Data policy

Do not commit recordings, copied papers, trained weights, secrets, generated
feature stores, or machine-specific corpus paths. Commit reproducible manifests,
source receipts, labels that may legally be shared, benchmark contracts, and
compact results. Every derivative must retain source coordinates, parameters,
tool versions, and dataset split identity.
