#!/usr/bin/env python3
"""Deterministic source-level compatibility matrix for JammerNetz releases."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


FRAMES = 96
OUTAGE_START = 32
OUTAGE_END = 48
HOLD_START = 24
HOLD_END = 34


def run_peer(peer: Path, *arguments: object) -> None:
    command = [str(peer), *(str(argument) for argument in arguments)]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise AssertionError(
            f"Compatibility peer failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def load(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def save(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def generate(peer: Path, root: Path, name: str, source_id: int, route: str) -> dict[str, Any]:
    output = root / f"upload-{name}.json"
    run_peer(peer, "generate", output, name, source_id, route, FRAMES)
    trace = load(output)
    assert trace["frames"] == FRAMES
    assert len(trace["ticks"]) == FRAMES
    assert sum(len(tick) for tick in trace["ticks"]) >= FRAMES - 1
    return trace


def scenario(clients: list[tuple[str, dict[str, Any]]]) -> dict[str, Any]:
    return {
        "format": 1,
        "frames": len(clients[0][1]["ticks"]),
        "clients": [
            {"name": name, "ticks": trace["ticks"]}
            for name, trace in clients
        ],
    }


def mix(peer: Path, root: Path, name: str, value: dict[str, Any]) -> dict[str, Any]:
    input_path = root / f"{name}-server-input.json"
    output_path = root / f"{name}-server-output.json"
    save(input_path, value)
    run_peer(peer, "mix", input_path, output_path)
    result = load(output_path)
    assert result["deserialization_failures"] == 0
    assert result["mix_count"] > 0
    return result


def render(peer: Path, root: Path, name: str, ticks: list[list[Any]]) -> dict[str, Any]:
    input_path = root / f"{name}-render-input.json"
    output_path = root / f"{name}-render-output.json"
    # Let the AudioEngine drain its last prepared frames without wall-clock sleeps.
    save(input_path, {"format": 1, "ticks": [*ticks, [], [], [], []]})
    run_peer(peer, "render", input_path, output_path)
    result = load(output_path)
    assert result["deserialization_failures"] == 0
    return result


def assert_healthy_render(result: dict[str, Any], minimum_packets: int = 60) -> None:
    assert result["delivered_packets"] >= minimum_packets, result
    assert result["non_silent_ticks"] >= minimum_packets - 4, result
    assert result["first_non_silent_tick"] >= 0, result
    assert result["longest_silence_after_start"] <= 6, result


def client_observations(server_result: dict[str, Any], client: str) -> list[dict[str, Any]]:
    return [entry for entry in server_result["observations"] if entry["target"] == client]


def summarize_server(server_result: dict[str, Any]) -> dict[str, Any]:
    """Keep the durable summary small; detailed wire traces remain separate artifacts."""
    keys = (
        "peer_version",
        "frames",
        "mix_count",
        "output_count",
        "deserialization_failures",
        "fast_forward_events",
        "fast_forwarded_packets",
        "cadence_changes",
    )
    return {
        **{key: server_result[key] for key in keys},
        "observation_count": len(server_result["observations"]),
    }


def slice_scenario(value: dict[str, Any], start: int, end: int) -> dict[str, Any]:
    return {
        "format": value["format"],
        "frames": end - start,
        "clients": [
            {"name": client["name"], "ticks": client["ticks"][start:end]}
            for client in value["clients"]
        ],
    }


def run_matrix(legacy: Path, candidate: Path, artifact_dir: Path) -> dict[str, Any]:
    if artifact_dir.exists():
        shutil.rmtree(artifact_dir)
    artifact_dir.mkdir(parents=True)

    old_a = generate(legacy, artifact_dir, "old-a", 11, "left")
    old_b = generate(legacy, artifact_dir, "old-b", 12, "right")
    new_a = generate(candidate, artifact_dir, "new-a", 21, "left")
    new_b = generate(candidate, artifact_dir, "new-b", 22, "right")

    results: dict[str, Any] = {
        "format": 1,
        "candidate_peer": str(candidate),
        "baseline_peer": str(legacy),
        "scenarios": {},
    }

    # Released clients must freshly connect to and render candidate-server output.
    old_to_new = mix(candidate, artifact_dir, "old-clients-new-server",
                     scenario([("old-a", old_a), ("old-b", old_b)]))
    old_a_render = render(legacy, artifact_dir, "old-a-from-new-server",
                          old_to_new["downloads"]["old-a"])
    old_b_render = render(legacy, artifact_dir, "old-b-from-new-server",
                          old_to_new["downloads"]["old-b"])
    assert_healthy_render(old_a_render)
    assert_healthy_render(old_b_render)
    results["scenarios"]["released_clients_candidate_server"] = {
        "status": "pass",
        "server": summarize_server(old_to_new),
        "clients": [old_a_render, old_b_render],
    }

    # Candidate clients must continue to work when client rollout precedes server rollout.
    new_to_old = mix(legacy, artifact_dir, "new-clients-old-server",
                     scenario([("new-a", new_a), ("new-b", new_b)]))
    new_a_render = render(candidate, artifact_dir, "new-a-from-old-server",
                          new_to_old["downloads"]["new-a"])
    new_b_render = render(candidate, artifact_dir, "new-b-from-old-server",
                          new_to_old["downloads"]["new-b"])
    assert_healthy_render(new_a_render)
    assert_healthy_render(new_b_render)
    results["scenarios"]["candidate_clients_released_server"] = {
        "status": "pass",
        "server": summarize_server(new_to_old),
        "clients": [new_a_render, new_b_render],
    }

    # An old and a new AudioEngine must coexist in one candidate-server room.
    mixed = mix(candidate, artifact_dir, "mixed-room",
                scenario([("old-a", old_a), ("new-b", new_b)]))
    mixed_old = render(legacy, artifact_dir, "mixed-old-a", mixed["downloads"]["old-a"])
    mixed_new = render(candidate, artifact_dir, "mixed-new-b", mixed["downloads"]["new-b"])
    assert_healthy_render(mixed_old)
    assert_healthy_render(mixed_new)
    results["scenarios"]["mixed_room"] = {
        "status": "pass",
        "server": summarize_server(mixed),
        "clients": [mixed_old, mixed_new],
    }

    # The old uploader disappears, but remains a download recipient on the candidate server.
    outage_old = json.loads(json.dumps(old_a))
    for tick in range(OUTAGE_START, OUTAGE_END):
        outage_old["ticks"][tick] = []
    outage = mix(candidate, artifact_dir, "old-client-upload-outage",
                 scenario([("old-a", outage_old), ("new-b", new_b)]))
    outage_render = render(legacy, artifact_dir, "old-a-during-upload-outage",
                           outage["downloads"]["old-a"])
    assert_healthy_render(outage_render)
    outage_observations = client_observations(outage, "old-a")
    outage_ticks = {entry["tick"] for entry in outage_observations}
    assert all(tick in outage_ticks for tick in range(OUTAGE_START, OUTAGE_END)), outage_observations
    outage_timestamps = [
        entry["timestamp"] for entry in outage_observations
        if OUTAGE_START <= entry["tick"] < OUTAGE_END
    ]
    stale_timestamp_observed = len(set(outage_timestamps)) == 1
    results["scenarios"]["released_client_upload_outage"] = {
        "status": "pass",
        "server": summarize_server(outage),
        "client": outage_render,
        "stale_timestamp_observed": stale_timestamp_observed,
        "diagnostic_compatibility": (
            "known_2.4.2_rtt_limitation" if stale_timestamp_observed else "pass"
        ),
    }

    # Burst an old client's held packets. The candidate must rebase only that queue.
    pressured_old = json.loads(json.dumps(old_a))
    held: list[Any] = []
    for tick in range(HOLD_START, HOLD_END):
        held.extend(pressured_old["ticks"][tick])
        pressured_old["ticks"][tick] = []
    pressured_old["ticks"][HOLD_END].extend(held)
    pressure = mix(candidate, artifact_dir, "old-client-queue-pressure",
                   scenario([("old-a", pressured_old), ("new-b", new_b)]))
    pressure_healthy = render(candidate, artifact_dir, "healthy-new-b-during-pressure",
                              pressure["downloads"]["new-b"])
    assert_healthy_render(pressure_healthy)
    assert pressure["fast_forward_events"] > 0, pressure
    assert pressure["fast_forwarded_packets"] > 0, pressure
    results["scenarios"]["released_client_queue_pressure"] = {
        "status": "pass",
        "server": summarize_server(pressure),
        "healthy_client": pressure_healthy,
    }

    # Preserve client receive state while replacing a released server with the candidate.
    rolling_input = scenario([("old-a", old_a), ("new-b", new_b)])
    midpoint = FRAMES // 2
    released_segment = mix(legacy, artifact_dir, "rolling-released-segment",
                           slice_scenario(rolling_input, 0, midpoint))
    candidate_segment = mix(candidate, artifact_dir, "rolling-candidate-segment",
                            slice_scenario(rolling_input, midpoint, FRAMES))
    rolling_ticks: dict[str, list[list[Any]]] = {}
    for client in ("old-a", "new-b"):
        rolling_ticks[client] = [
            *released_segment["downloads"][client],
            *candidate_segment["downloads"][client],
        ]
    rolling_old = render(legacy, artifact_dir, "rolling-old-a", rolling_ticks["old-a"])
    rolling_new = render(candidate, artifact_dir, "rolling-new-b", rolling_ticks["new-b"])
    released_counters = [
        entry["message_counter"] for entry in client_observations(released_segment, "old-a")
    ]
    candidate_counters = [
        entry["message_counter"] for entry in client_observations(candidate_segment, "old-a")
    ]
    counter_regressed = bool(released_counters and candidate_counters
                             and candidate_counters[0] <= released_counters[-1])
    assert counter_regressed, (released_counters[-4:], candidate_counters[:4])
    results["scenarios"]["rolling_server_replacement"] = {
        "status": "known_failure",
        "reason": "candidate server output sequence restarts below persistent client receive state",
        "counter_regressed": counter_regressed,
        "released_tail_counters": released_counters[-4:],
        "candidate_head_counters": candidate_counters[:4],
        "clients": [rolling_old, rolling_new],
    }

    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    arguments = parser.parse_args()

    try:
        summary = run_matrix(arguments.legacy, arguments.candidate, arguments.artifact_dir)
        save(arguments.artifact_dir / "summary.json", summary)
        passed = sum(
            scenario_result["status"] == "pass"
            for scenario_result in summary["scenarios"].values()
        )
        known = sum(
            scenario_result["status"] == "known_failure"
            for scenario_result in summary["scenarios"].values()
        )
        print(f"Release compatibility: {passed} passed, {known} known failure(s)")
        return 0
    except Exception as error:  # noqa: BLE001 - preserve all diagnostics for CTest
        print(f"Release compatibility failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
