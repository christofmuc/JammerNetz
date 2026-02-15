#!/usr/bin/env bash
set -euo pipefail

mkdir -p logs
mkdir -p logs/cores
stamp=$(date +"%Y%m%d-%H%M%S")

client1_stdout="logs/client1-${stamp}.stdout.log"
client2_stdout="logs/client2-${stamp}.stdout.log"
server_stdout="logs/server-${stamp}.stdout.log"

# Enable core dumps for this shell and all spawned processes.
ulimit -c unlimited || true
# Include anonymous/private mappings in core files for better post-mortem diagnostics.
echo 0x3f > /proc/self/coredump_filter 2>/dev/null || true

core_limit="$(ulimit -c || true)"
core_pattern="$(cat /proc/sys/kernel/core_pattern 2>/dev/null || echo unknown)"
core_uses_pid="$(cat /proc/sys/kernel/core_uses_pid 2>/dev/null || echo unknown)"

# Buffer tuning for WSL/PulseAudio test environments.
# Derivation from common/BuffersConfig.h:
# blocks ~= milliseconds * SAMPLE_RATE / (SAMPLE_BUFFER_SIZE * 1000)
# with SAMPLE_RATE=48000 and SAMPLE_BUFFER_SIZE=128 => ~0.375 blocks/ms.
server_buffer_blocks=38   # ~100 ms incoming jitter buffer
server_wait_blocks=56     # ~150 ms maximum wait buffer
server_prefill_blocks=38  # ~100 ms prefill on connect

echo "Starting test run ${stamp}"
echo "Client 1 stdout: ${client1_stdout}"
echo "Client 2 stdout: ${client2_stdout}"
echo "Server stdout:   ${server_stdout}"
echo "Core limit:      ${core_limit}"
echo "Core pattern:    ${core_pattern}"
echo "Core uses pid:   ${core_uses_pid}"
echo "Server buffers:  --buffer=${server_buffer_blocks} --wait=${server_wait_blocks} --prefill=${server_prefill_blocks}"

client1_pid=""
client2_pid=""
analysis_done=0

cleanup() {
	if [[ -n "${client1_pid}" ]]; then
		kill "${client1_pid}" 2>/dev/null || true
	fi
	if [[ -n "${client2_pid}" ]]; then
		kill "${client2_pid}" 2>/dev/null || true
	fi
}

analyze_logs() {
	if [[ "${analysis_done}" -ne 0 ]]; then
		return
	fi
	analysis_done=1

	if [[ -f "./log_analyze.py" ]]; then
		echo "Analyzing logs for run ${stamp} ..."
		python3 ./log_analyze.py --logs-dir logs --stamp "${stamp}" || true
	fi
}

on_exit() {
	exit_code=$?
	trap - EXIT INT TERM
	cleanup
	analyze_logs
	exit "${exit_code}"
}

trap on_exit EXIT
trap 'exit 130' INT TERM

JN_REMOTE_LOG_ENABLE=1 JN_REMOTE_LOG_NAME="client1-remote-${stamp}" ./builds/Client/JammerNetzClient --auto-start-audio >"${client1_stdout}" 2>&1 &
client1_pid=$!
JN_REMOTE_LOG_ENABLE=1 JN_REMOTE_LOG_NAME="client2-remote-${stamp}" ./builds/Client/JammerNetzClient --clientID=Zweiter --auto-start-audio >"${client2_stdout}" 2>&1 &
client2_pid=$!

JN_REMOTE_LOG_ENABLE=1 JN_REMOTE_LOG_NAME="server-remote-${stamp}" ./builds/Server/JammerNetzServer --buffer="${server_buffer_blocks}" --wait="${server_wait_blocks}" --prefill="${server_prefill_blocks}" | tee "${server_stdout}"
