#!/usr/bin/env python3
"""Harvest one SMC conference year from the public Zenodo community API."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


API_ROOT = "https://zenodo.org/api/communities/smc/records"
PAGE_SIZE = 25  # Zenodo's anonymous-request limit.


def fetch_json(url: str) -> dict:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/json",
            "User-Agent": "JammerNetz-gamification-research/1.0",
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def ordinal(value: int) -> str:
    if 10 <= value % 100 <= 20:
        suffix = "th"
    else:
        suffix = {1: "st", 2: "nd", 3: "rd"}.get(value % 10, "th")
    return f"{value}{suffix}"


def verify_corpus(records: list[dict], year: int) -> dict:
    edition = year - 2003
    allowed_meetings = {
        f"Sound and Music Computing {year}",
        f"Sound and Music Computing Conference {year}",
        f"{ordinal(edition)} Sound and Music Computing Conference",
    }
    mismatches = []
    resource_types: dict[str, int] = {}
    meeting_titles: dict[str, int] = {}

    for record in records:
        metadata = record.get("metadata", {})
        meeting = metadata.get("meeting") or {}
        meeting_title = meeting.get("title", "")
        resource_type = (metadata.get("resource_type") or {}).get("title", "")
        title = metadata.get("title", "")
        resource_types[resource_type] = resource_types.get(resource_type, 0) + 1
        meeting_titles[meeting_title] = meeting_titles.get(meeting_title, 0) + 1

        is_paper = (
            resource_type == "Conference paper" and meeting_title in allowed_meetings
        )
        is_proceedings = (
            resource_type == "Book"
            and title.startswith(f"Proceedings of the {ordinal(edition)} Sound and Music Computing Conference")
            and "Sound and Music Computing" in meeting_title
        )
        if not (is_paper or is_proceedings):
            mismatches.append(str(record.get("id")))

    if mismatches:
        raise RuntimeError(
            "The publication-date query returned records outside the expected "
            f"conference corpus: {', '.join(mismatches)}"
        )

    return {
        "meeting_titles": dict(sorted(meeting_titles.items())),
        "resource_types": dict(sorted(resource_types.items())),
    }


def harvest(year: int) -> tuple[list[dict], str, dict]:
    query = f"metadata.publication_date:[{year}-01-01 TO {year}-12-31]"
    records: list[dict] = []
    total: int | None = None
    page = 1

    while total is None or len(records) < total:
        params = urllib.parse.urlencode(
            {
                "q": query,
                "size": PAGE_SIZE,
                "page": page,
                "sort": "oldest",
            }
        )
        payload = fetch_json(f"{API_ROOT}?{params}")
        if total is None:
            total = int(payload["hits"]["total"])
        hits = payload["hits"]["hits"]
        if not hits:
            break
        records.extend(hits)
        page += 1

    if total is None or len(records) != total:
        raise RuntimeError(f"Expected {total} records but harvested {len(records)}")

    ids = [str(record["id"]) for record in records]
    if len(ids) != len(set(ids)):
        raise RuntimeError("Zenodo response contains duplicate record IDs")

    return records, query, verify_corpus(records, year)


def write_inventory(path: Path, records: list[dict], provenance: dict) -> str:
    if path.exists():
        raise FileExistsError(f"Refusing to overwrite immutable inventory: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    with path.open("wb") as output:
        for record in records:
            item = {
                "provenance": provenance,
                "zenodo_record": record,
            }
            line = (
                json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
                + "\n"
            ).encode("utf-8")
            output.write(line)
            digest.update(line)
    return digest.hexdigest()


def write_receipt(
    path: Path,
    *,
    year: int,
    count: int,
    query: str,
    retrieved_at: str,
    inventory_path: Path,
    sha256: str,
    verification: dict,
) -> None:
    if path.exists():
        raise FileExistsError(f"Refusing to overwrite immutable receipt: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    relative_inventory = Path(os.path.relpath(inventory_path, path.parent)).as_posix()
    resource_types = "; ".join(
        f"{name}: {count}" for name, count in verification["resource_types"].items()
    )
    meeting_titles = "; ".join(
        f"{name}: {count}" for name, count in verification["meeting_titles"].items()
    )
    content = f"""# SMC {year} Zenodo inventory receipt

- Source ID: `smc-{year}-zenodo-inventory`
- Community: Sound and Music Computing Conference (`smc`)
- API endpoint: {API_ROOT}
- Query: `{query}`
- Retrieved at: {retrieved_at}
- Records: {count}
- Resource types: {resource_types}
- Meeting titles: {meeting_titles}
- Conference identity check: every record is either a conference paper with a recognized SMC edition title or that edition's complete proceedings book
- Inventory: [`{relative_inventory}`]({relative_inventory})
- SHA-256: `{sha256}`

The JSONL inventory preserves the public API response for every record. It does
not contain downloaded PDFs. Screening and interpretation belong in the wiki
layer and must not modify this receipt or inventory.
"""
    path.write_text(content, encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--year", type=int, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--receipt", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output or root / "raw" / "inventories" / f"smc-{args.year}.jsonl"
    receipt = args.receipt or root / "raw" / "sources" / f"smc-{args.year}-inventory.md"
    retrieved_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat()

    existing = [path for path in (output, receipt) if path.exists()]
    if existing:
        for path in existing:
            print(f"error: refusing to overwrite immutable output: {path}", file=sys.stderr)
        return 1

    try:
        records, query, verification = harvest(args.year)
        provenance = {
            "community": "smc",
            "endpoint": API_ROOT,
            "query": query,
            "retrieved_at": retrieved_at,
        }
        sha256 = write_inventory(output, records, provenance)
        write_receipt(
            receipt,
            year=args.year,
            count=len(records),
            query=query,
            retrieved_at=retrieved_at,
            inventory_path=output,
            sha256=sha256,
            verification=verification,
        )
    except (OSError, RuntimeError, KeyError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"wrote {len(records)} records to {output}")
    print(f"sha256 {sha256}")
    print(f"wrote receipt to {receipt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
