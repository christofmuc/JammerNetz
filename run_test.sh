#!/usr/bin/env bash
set -euo pipefail

mkdir -p logs
stamp=$(date +"%Y%m%d-%H%M%S")

client1_stdout="logs/client1-${stamp}.stdout.log"
client2_stdout="logs/client2-${stamp}.stdout.log"
server_stdout="logs/server-${stamp}.stdout.log"

echo "Starting test run ${stamp}"
echo "Client 1 stdout: ${client1_stdout}"
echo "Client 2 stdout: ${client2_stdout}"
echo "Server stdout:   ${server_stdout}"

JN_REMOTE_LOG_ENABLE=1 JN_REMOTE_LOG_NAME="client1-remote-${stamp}" ./builds/Client/JammerNetzClient >"${client1_stdout}" 2>&1 &
client1_pid=$!
JN_REMOTE_LOG_ENABLE=1 JN_REMOTE_LOG_NAME="client2-remote-${stamp}" ./builds/Client/JammerNetzClient --clientID=Zweiter >"${client2_stdout}" 2>&1 &
client2_pid=$!

cleanup() {
	kill "${client1_pid}" "${client2_pid}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

JN_REMOTE_LOG_ENABLE=1 JN_REMOTE_LOG_NAME="server-remote-${stamp}" ./builds/Server/JammerNetzServer | tee "${server_stdout}"
