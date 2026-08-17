#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

sudo apt-get update
sudo apt-get install --yes --no-install-recommends \
  file \
  iproute2 \
  libasound2t64 \
  libcurl4t64 \
  libfontconfig1 \
  libfreetype6 \
  libncurses6 \
  libtbb12 \
  libx11-6 \
  libxext6

if ! getent group jammernetz >/dev/null; then
  sudo groupadd --system jammernetz
fi

if ! id jammernetz >/dev/null 2>&1; then
  sudo useradd \
    --system \
    --gid jammernetz \
    --home-dir /var/lib/jammernetz \
    --create-home \
    --shell /usr/sbin/nologin \
    jammernetz
fi

sudo install -d -o root -g jammernetz -m 0750 /etc/jammernetz
sudo install -d -o jammernetz -g jammernetz -m 0750 /var/lib/jammernetz
sudo install -o root -g root -m 0755 /tmp/JammerNetzServer /usr/local/bin/JammerNetzServer
sudo install -o root -g root -m 0644 /tmp/jammernetz-server.service /etc/systemd/system/jammernetz-server.service

file /usr/local/bin/JammerNetzServer
ldd /usr/local/bin/JammerNetzServer | tee /tmp/jammernetz-runtime-dependencies.txt
if grep -q "not found" /tmp/jammernetz-runtime-dependencies.txt; then
  echo "JammerNetzServer has unresolved runtime dependencies:" >&2
  cat /tmp/jammernetz-runtime-dependencies.txt >&2
  exit 1
fi

sudo systemctl daemon-reload
sudo systemctl enable jammernetz-server.service

sudo apt-get clean
sudo rm -rf /var/lib/apt/lists/*
