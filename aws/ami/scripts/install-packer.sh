#!/usr/bin/env bash
set -euo pipefail

packer_version="${PACKER_VERSION:-1.16.0}"

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
  echo "This installer intentionally installs the x86_64 Linux Packer CLI." >&2
  exit 1
fi

install_dir="${PACKER_INSTALL_DIR:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/packer-bin}"
archive="packer_${packer_version}_linux_amd64.zip"
base_url="https://releases.hashicorp.com/packer/${packer_version}"
temporary_dir="$(mktemp -d)"
trap 'rm -rf "${temporary_dir}"' EXIT

curl --fail --silent --show-error --location \
  --output "${temporary_dir}/${archive}" \
  "${base_url}/${archive}"
curl --fail --silent --show-error --location \
  --output "${temporary_dir}/SHA256SUMS" \
  "${base_url}/packer_${packer_version}_SHA256SUMS"

(
  cd "${temporary_dir}"
  grep " ${archive}$" SHA256SUMS | sha256sum --check --strict -
  unzip -q "${archive}"
)

mkdir -p "${install_dir}"
install -m 0755 "${temporary_dir}/packer" "${install_dir}/packer"

if [[ -n "${GITHUB_PATH:-}" ]]; then
  echo "${install_dir}" >> "${GITHUB_PATH}"
fi

"${install_dir}/packer" version
