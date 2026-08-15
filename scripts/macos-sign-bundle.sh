#!/bin/bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 BUNDLE DEVELOPER_ID_APPLICATION [KEYCHAIN]" >&2
    exit 2
fi

bundle=$1
identity=$2
keychain=${3:-}

if [[ ! -d "$bundle" ]]; then
    echo "Bundle does not exist: $bundle" >&2
    exit 1
fi

codesign_args=(--force --timestamp --options runtime --sign "$identity")
if [[ -n "$keychain" ]]; then
    codesign_args+=(--keychain "$keychain")
fi

sign_item() {
    echo "Signing $1"
    /usr/bin/codesign "${codesign_args[@]}" "$1"
}

# Sign copied dynamic code before its containing bundle. AU/VST3 fix-up puts
# dylibs beside the plug-in executable, while application fix-up normally uses
# Contents/Frameworks.
contents="$bundle/Contents"
if [[ -d "$contents" ]]; then
    while IFS= read -r -d '' item; do
        sign_item "$item"
    done < <(/usr/bin/find "$contents" -type f \( -name '*.dylib' -o -name '*.so' \) -print0)
fi

frameworks="$contents/Frameworks"
if [[ -d "$frameworks" ]]; then
    while IFS= read -r -d '' item; do
        sign_item "$item"
    done < <(/usr/bin/find "$frameworks" -type f -perm -111 ! -name '*.dylib' ! -name '*.so' -print0)
fi

# Nested executable bundles must be signed inside-out. `find -depth` visits
# children before their parents.
if [[ -d "$contents" ]]; then
    while IFS= read -r -d '' item; do
        sign_item "$item"
    done < <(/usr/bin/find "$contents" -depth -type d \( -name '*.framework' -o -name '*.app' -o -name '*.appex' -o -name '*.xpc' -o -name '*.bundle' \) -print0)
fi

sign_item "$bundle"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$bundle"
