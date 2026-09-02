#!/usr/bin/env python3
"""Import or verify an immutable released-source compatibility snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


SNAPSHOT_PATHS = ("Client/Source", "common", "Server/Source")
MANIFEST_NAME = "MANIFEST.json"


def digest(path: Path) -> str:
    # Git may materialize these source files with LF or CRLF depending on the
    # checkout platform. Hash their canonical text rather than platform-local
    # line endings so an unchanged snapshot verifies everywhere.
    canonical = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return hashlib.sha256(canonical).hexdigest()


def snapshot_files(destination: Path) -> dict[str, str]:
    return {
        path.relative_to(destination).as_posix(): digest(path)
        for path in sorted(destination.rglob("*"))
        if path.is_file() and path.name != MANIFEST_NAME
    }


def write_manifest(destination: Path, release: str, tag: str, commit: str) -> None:
    manifest = {
        "format": 1,
        "release": release,
        "tag": tag,
        "commit": commit,
        "paths": list(SNAPSHOT_PATHS),
        "hash_normalization": "CRLF and CR line endings are normalized to LF",
        "files": snapshot_files(destination),
        "build_note": (
            "Frozen JammerNetz production sources; compatibility adapters and "
            "current toolchain dependencies live outside this directory."
        ),
    }
    with (destination / MANIFEST_NAME).open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")


def import_snapshot(repository: Path, destination: Path, release: str, tag: str) -> None:
    if destination.exists():
        raise RuntimeError(f"Snapshot destination already exists: {destination}")
    commit = subprocess.check_output(
        ["git", "-c", f"safe.directory={repository.as_posix()}", "rev-list", "-n", "1", tag],
        cwd=repository,
        text=True,
    ).strip()
    destination.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix="jammernetz-compat-") as temporary:
        archive = Path(temporary) / "snapshot.zip"
        subprocess.run(
            [
                "git", "-c", f"safe.directory={repository.as_posix()}", "archive",
                "--format=zip", f"--output={archive}", tag, *SNAPSHOT_PATHS,
            ],
            cwd=repository,
            check=True,
        )
        with zipfile.ZipFile(archive) as source:
            source.extractall(destination)
    write_manifest(destination, release, tag, commit)


def verify_snapshot(destination: Path) -> None:
    manifest_path = destination / MANIFEST_NAME
    if not manifest_path.is_file():
        raise RuntimeError(f"Missing snapshot manifest: {manifest_path}")
    with manifest_path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    expected = manifest.get("files", {})
    actual = snapshot_files(destination)
    missing = sorted(set(expected) - set(actual))
    unexpected = sorted(set(actual) - set(expected))
    changed = sorted(path for path in set(expected) & set(actual) if expected[path] != actual[path])
    if missing or unexpected or changed:
        raise RuntimeError(
            "Snapshot verification failed: "
            f"missing={missing}, unexpected={unexpected}, changed={changed}"
        )
    if manifest.get("commit") != "8dbad6aa4c86ea982632cf3da5416a8a7a4ac734":
        raise RuntimeError(f"Unexpected 2.4.2 baseline commit: {manifest.get('commit')}")
    print(f"Verified {len(actual)} frozen files for release {manifest.get('release')}")


def main() -> int:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--verify", action="store_true")
    action.add_argument("--import-snapshot", action="store_true")
    action.add_argument("--refresh-manifest", action="store_true")
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--repository", type=Path)
    parser.add_argument("--release", default="2.4.2")
    parser.add_argument("--tag", default="2.4.2")
    arguments = parser.parse_args()

    if arguments.verify:
        verify_snapshot(arguments.destination)
        return 0
    if arguments.refresh_manifest:
        write_manifest(
            arguments.destination,
            arguments.release,
            arguments.tag,
            "8dbad6aa4c86ea982632cf3da5416a8a7a4ac734",
        )
        return 0
    if arguments.repository is None:
        parser.error("--repository is required with --import-snapshot")
    import_snapshot(
        arguments.repository.resolve(), arguments.destination.resolve(),
        arguments.release, arguments.tag,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
