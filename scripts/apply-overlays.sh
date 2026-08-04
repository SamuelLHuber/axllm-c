#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ax_root="$repo_root/vendor/ax"

test -d "$ax_root" || { echo "missing vendor/ax; run scripts/update-upstream.sh" >&2; exit 1; }

mkdir -p "$ax_root/tools/axir/internal/axir/templates/cpp"
cp "$repo_root/overlays/ax/cppCHeader.h" "$ax_root/tools/axir/internal/axir/templates/cpp/cppCHeader.h"
cp "$repo_root/overlays/ax/cppCSource.cpp" "$ax_root/tools/axir/internal/axir/templates/cpp/cppCSource.cpp"
cp "$repo_root/overlays/ax/cppCSignatureSchemaExample.cpp" "$ax_root/tools/axir/internal/axir/templates/cpp/cppCSignatureSchemaExample.cpp"
node "$repo_root/scripts/patch-ax-codegen.mjs" "$ax_root"

echo "applied ax C ABI overlays"
