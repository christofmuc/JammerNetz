#!/usr/bin/env python3
"""
Analyze JammerNetz run logs and print quick statistics/anomalies.

Usage:
  python3 log_analyze.py
  python3 log_analyze.py --stamp 20260215-112804
  python3 log_analyze.py --logs-dir logs --strict
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple


TIME_EVENT_RE = re.compile(r"^(?P<time>\d\d:\d\d:\d\d\.\d{3}) \[(?P<tag>[^\]]+)\] (?P<msg>.*)$")
STAMP_FROM_SERVER_STDOUT_RE = re.compile(r"^server-(\d{8}-\d{6})\.stdout\.log$")
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]|\x1b[@-_]")

SEND_RE = re.compile(
    r"SetRemoteVolume targetClientId=(?P<target_client>\d+) "
    r"targetChannel=(?P<target_channel>\d+) vol=(?P<vol>[0-9.]+) seq=(?P<seq>\d+)"
)
APPLY_RE = re.compile(
    r"ApplyLocalVolume sourceClientId=(?P<source_client>\d+) "
    r"targetChannel=(?P<target_channel>-?\d+) controllerIndex=(?P<controller>\d+) "
    r"vol=(?P<vol>[0-9.]+) seq=(?P<seq>\d+|none)"
)
SERVER_REV_RE = re.compile(r"session revision=(?P<rev>\d+)")
DROP_RE = re.compile(r"drop ")
ROUTING_DRIFT_RE = re.compile(r"routing drift ")

STDOUT_ERROR_PATTERNS = [
    re.compile(pat, re.IGNORECASE)
    for pat in [
        r"fatal",
        r"overflow",
        r"send queue length overflow",
        r"malloc\(\)",
        r"unaligned tcache",
        r"aborted",
        r"assert",
        r"network down",
        r"disconnect",
        r"error",
    ]
]


@dataclass
class SequenceSummary:
    count: int
    first: Optional[int]
    last: Optional[int]
    missing: List[int]
    non_monotonic_count: int


def summarize_sequence(values: List[int]) -> SequenceSummary:
    if not values:
        return SequenceSummary(0, None, None, [], 0)

    first = values[0]
    last = values[-1]
    missing: List[int] = []
    non_monotonic_count = 0

    for idx in range(1, len(values)):
        delta = values[idx] - values[idx - 1]
        if delta <= 0:
            non_monotonic_count += 1
        elif delta > 1:
            missing.extend(range(values[idx - 1] + 1, values[idx]))

    return SequenceSummary(
        count=len(values),
        first=first,
        last=last,
        missing=missing,
        non_monotonic_count=non_monotonic_count,
    )


def find_latest_stamp(logs_dir: pathlib.Path) -> Optional[str]:
    stamps: List[str] = []
    for path in logs_dir.iterdir():
        match = STAMP_FROM_SERVER_STDOUT_RE.match(path.name)
        if match:
            stamps.append(match.group(1))
    if not stamps:
        return None
    return sorted(stamps)[-1]


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except FileNotFoundError:
        return ""


def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE_RE.sub("", text).replace("\r", "\n")


def parse_remote_file(path: pathlib.Path) -> Tuple[collections.Counter, List[int], List[int], List[str]]:
    counts: collections.Counter = collections.Counter()
    sent_sequences: List[int] = []
    apply_sequences: List[int] = []
    drop_lines: List[str] = []

    text = read_text(path)
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = TIME_EVENT_RE.match(line)
        if not match:
            continue
        tag = match.group("tag")
        msg = match.group("msg")
        counts[f"tag:{tag}"] += 1

        send_match = SEND_RE.search(msg)
        if send_match:
            sent_sequences.append(int(send_match.group("seq")))

        apply_match = APPLY_RE.search(msg)
        if apply_match and apply_match.group("seq") != "none":
            apply_sequences.append(int(apply_match.group("seq")))

        if DROP_RE.search(msg):
            counts["drops_total"] += 1
            counts[f"drops_by_tag:{tag}"] += 1
            drop_lines.append(line)
        if ROUTING_DRIFT_RE.search(msg):
            counts["routing_drift_total"] += 1

    return counts, sent_sequences, apply_sequences, drop_lines


def parse_server_revisions(path: pathlib.Path) -> List[int]:
    revisions: List[int] = []
    text = read_text(path)
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = TIME_EVENT_RE.match(line)
        if not match:
            continue
        msg = match.group("msg")
        rev_match = SERVER_REV_RE.search(msg)
        if rev_match:
            revisions.append(int(rev_match.group("rev")))
    return revisions


def scan_stdout_anomalies(path: pathlib.Path) -> List[str]:
    anomalies: List[str] = []
    text = strip_ansi(read_text(path))
    if not text:
        return anomalies
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        for pattern in STDOUT_ERROR_PATTERNS:
            if pattern.search(stripped):
                anomalies.append(stripped)
                break
    return anomalies


def short_list(values: Iterable[int], max_items: int = 10) -> str:
    data = list(values)
    if not data:
        return "-"
    if len(data) <= max_items:
        return ",".join(str(v) for v in data)
    shown = ",".join(str(v) for v in data[:max_items])
    return f"{shown},...(+{len(data) - max_items})"


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze JammerNetz test logs.")
    parser.add_argument("--logs-dir", default="logs", help="Directory containing run logs (default: logs)")
    parser.add_argument("--stamp", default=None, help="Run stamp (e.g. 20260215-112804). Defaults to latest.")
    parser.add_argument("--strict", action="store_true", help="Exit with code 1 if anomalies are detected.")
    args = parser.parse_args()

    logs_dir = pathlib.Path(args.logs_dir)
    if not logs_dir.exists():
        print(f"ERROR: logs directory not found: {logs_dir}")
        return 2

    stamp = args.stamp or find_latest_stamp(logs_dir)
    if not stamp:
        print("ERROR: no run stamp found in logs directory")
        return 2

    server_stdout = logs_dir / f"server-{stamp}.stdout.log"
    server_remote = logs_dir / f"server-remote-{stamp}.log"
    client_remote_files = sorted(logs_dir.glob(f"client*-remote-{stamp}.log"))

    print(f"Log analysis for run {stamp}")
    print(f"- server stdout: {server_stdout}")
    print(f"- server remote: {server_remote}")
    print(f"- client remotes: {', '.join(str(p) for p in client_remote_files) if client_remote_files else '-'}")

    anomalies: List[str] = []

    server_revisions = parse_server_revisions(server_remote)
    unique_revisions = sorted(set(server_revisions))
    server_rev_summary = summarize_sequence(unique_revisions)
    print("\nServer revision flow")
    print(
        f"- revision events: {len(server_revisions)} | unique revisions: {server_rev_summary.count} "
        f"| first:last = {server_rev_summary.first}:{server_rev_summary.last}"
    )
    if server_rev_summary.missing:
        anomalies.append(f"server revisions missing values: {short_list(server_rev_summary.missing)}")
    if server_rev_summary.non_monotonic_count:
        anomalies.append(f"server revisions non-monotonic events: {server_rev_summary.non_monotonic_count}")

    total_send = 0
    total_apply = 0
    total_drops = 0

    print("\nClient remote-control flow")
    if not client_remote_files:
        print("- no client remote logs found")
        anomalies.append("no client remote logs found")

    for client_path in client_remote_files:
        counts, send_sequences, apply_sequences, drop_lines = parse_remote_file(client_path)
        send_summary = summarize_sequence(send_sequences)
        apply_summary = summarize_sequence(apply_sequences)

        total_send += send_summary.count
        total_apply += apply_summary.count
        total_drops += counts.get("drops_total", 0)

        print(f"- {client_path.name}")
        print(
            f"  send count={send_summary.count} seq={send_summary.first}:{send_summary.last} "
            f"missing={len(send_summary.missing)} non_monotonic={send_summary.non_monotonic_count}"
        )
        print(
            f"  apply count={apply_summary.count} seq={apply_summary.first}:{apply_summary.last} "
            f"missing={len(apply_summary.missing)} non_monotonic={apply_summary.non_monotonic_count}"
        )
        print(f"  drops={counts.get('drops_total', 0)}")
        print(f"  routing_drift={counts.get('routing_drift_total', 0)}")

        if send_summary.missing:
            anomalies.append(f"{client_path.name}: send sequence gaps {short_list(send_summary.missing)}")
        if send_summary.non_monotonic_count:
            anomalies.append(f"{client_path.name}: send non-monotonic count {send_summary.non_monotonic_count}")
        if apply_summary.missing:
            anomalies.append(f"{client_path.name}: apply sequence gaps {short_list(apply_summary.missing)}")
        if apply_summary.non_monotonic_count:
            anomalies.append(f"{client_path.name}: apply non-monotonic count {apply_summary.non_monotonic_count}")
        if drop_lines:
            anomalies.append(f"{client_path.name}: drop lines present ({len(drop_lines)})")
        if counts.get("routing_drift_total", 0):
            anomalies.append(f"{client_path.name}: routing drift detected ({counts.get('routing_drift_total')})")

    print("\nCross-check")
    print(f"- total sends: {total_send}")
    print(f"- total applies: {total_apply}")
    print(f"- total drops in client logs: {total_drops}")
    if total_apply == 0:
        anomalies.append("no client.apply entries found (older binary or apply logging missing)")
    elif total_apply > total_send:
        anomalies.append(f"apply count ({total_apply}) exceeds send count ({total_send})")

    stdout_anomalies = scan_stdout_anomalies(server_stdout)
    print("\nServer stdout scan")
    print(f"- suspicious lines: {len(stdout_anomalies)}")
    for line in stdout_anomalies[:10]:
        print(f"  {line}")
    if len(stdout_anomalies) > 10:
        print(f"  ... (+{len(stdout_anomalies) - 10} more)")
    if stdout_anomalies:
        anomalies.append(f"server stdout contains {len(stdout_anomalies)} suspicious lines")

    print("\nResult")
    if anomalies:
        print("- anomalies detected:")
        for item in anomalies:
            print(f"  - {item}")
    else:
        print("- no anomalies detected")

    return 1 if args.strict and anomalies else 0


if __name__ == "__main__":
    sys.exit(main())
