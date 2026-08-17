#!/usr/bin/env bash
set -euo pipefail

service_name="jammernetz-server.service"
runtime_dir="/run/jammernetz"
runtime_key="${runtime_dir}/secret.key"
service_key="/etc/jammernetz/secret.key"

cleanup() {
  sudo systemctl stop "${service_name}" >/dev/null 2>&1 || true
  sudo rm -f "${service_key}" "${runtime_key}"
  sudo systemctl reset-failed "${service_name}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

if [[ -e "${service_key}" || -L "${service_key}" ]]; then
  echo "Refusing to overwrite an existing JammerNetz session key." >&2
  exit 1
fi

sudo install -d -o root -g jammernetz -m 0750 "${runtime_dir}"
head -c 72 /dev/urandom | sudo tee "${runtime_key}" >/dev/null
sudo chown root:jammernetz "${runtime_key}"
sudo chmod 0640 "${runtime_key}"
sudo ln -s "${runtime_key}" "${service_key}"

sudo systemctl start "${service_name}"

for _ in {1..20}; do
  if sudo systemctl is-active --quiet "${service_name}"; then
    break
  fi
  sleep 1
done

if ! sudo systemctl is-active --quiet "${service_name}"; then
  sudo journalctl --unit "${service_name}" --no-pager --lines 100 >&2
  exit 1
fi

port_is_open=false
for _ in {1..20}; do
  if sudo ss -H -lunp | grep ':7777' >/dev/null; then
    port_is_open=true
    break
  fi
  if ! sudo systemctl is-active --quiet "${service_name}"; then
    break
  fi
  sleep 1
done

if [[ "${port_is_open}" != "true" ]]; then
  sudo journalctl --unit "${service_name}" --no-pager --lines 100 >&2
  echo "JammerNetzServer did not open UDP port 7777." >&2
  exit 1
fi

sudo systemctl stop "${service_name}"
sudo rm -f "${service_key}" "${runtime_key}"

if [[ -e "${service_key}" || -L "${service_key}" ]]; then
  echo "The temporary smoke-test key was not removed." >&2
  exit 1
fi

sudo journalctl --rotate
sudo journalctl --vacuum-time=1s >/dev/null

echo "JammerNetzServer systemd smoke test passed."
