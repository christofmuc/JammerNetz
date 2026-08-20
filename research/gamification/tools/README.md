# Research tools

## Harvest an SMC conference year

`harvest_smc.py` queries the public Zenodo `smc` community, paginates within
the anonymous API limit, and verifies that every result belongs to the expected
conference edition before writing an immutable JSONL inventory and receipt.
It accepts the historical meeting-title conventions used by Zenodo and includes
an edition's complete proceedings-book record when present.

From the repository root:

```powershell
python research/gamification/tools/harvest_smc.py --year 2025
```

The command refuses to overwrite an existing inventory or receipt. Delete or
rename neither during ordinary research: a changed upstream response should be
captured as a separately named retrieval.
