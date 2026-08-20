# Raw source layer

This directory contains immutable receipts for curated sources. It is the
provenance layer, not the place for LLM-written synthesis.

Each receipt should record:

- stable source ID;
- title, authors, year, and venue;
- DOI or canonical URL;
- source type and license when known;
- retrieval date;
- checksum when a local copy was inspected;
- links to code, models, datasets, or supplementary material.

The actual paper or recording may live in a local cache or external corpus.
Do not commit large PDFs, audio, MIDI, trained weights, or derived feature
stores here. Summaries and JammerNetz interpretations belong under
[`../wiki/sources/`](../wiki/sources/).
