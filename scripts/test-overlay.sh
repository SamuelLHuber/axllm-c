#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/vendor"
cp -a "$repo_root/vendor/ax" "$tmp/vendor/ax"
rm -rf "$tmp/vendor/ax/node_modules"
mkdir -p "$tmp/overlays" "$tmp/scripts"
cp -a "$repo_root/overlays/ax" "$tmp/overlays/ax"
cp "$repo_root/scripts/apply-overlays.sh" "$repo_root/scripts/patch-ax-codegen.mjs" "$tmp/scripts/"
(
  cd "$tmp"
  ./scripts/apply-overlays.sh
  ./scripts/apply-overlays.sh
)

grep -q 'axllm/axllm_c.h' "$tmp/vendor/ax/tools/axir/internal/axir/codegen.go"
grep -q 'var cppCHeader string' "$tmp/vendor/ax/tools/axir/internal/axir/templates_embed.go"
grep -q 'axllm/axllm_c.cpp' "$tmp/vendor/ax/tools/axir/internal/axir/templates/package/cppCMakeLists.cmake"

echo "overlay test ok"
