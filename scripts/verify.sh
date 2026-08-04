#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${AXLLM_C_BUILD_DIR:-/tmp/axllm-c-build}"

if [[ ! -d "$repo_root/dist" ]]; then
  echo "missing dist; run scripts/update-upstream.sh first" >&2
  exit 1
fi

rm -rf "$build_dir"
cmake -S "$repo_root/dist" -B "$build_dir" -DAX_BUILD_EXAMPLES=ON -DAX_BUILD_CONFORMANCE=OFF
cmake --build "$build_dir"
"$build_dir/signature_schema"
"$build_dir/c_signature_schema"

test -f "$repo_root/dist/axllm/axllm_c.h"
test -f "$repo_root/dist/axllm/axllm_c.cpp"
grep -q 'extern "C"' "$repo_root/dist/axllm/axllm_c.h"
grep -q 'axllm_c_schema_for_signature' "$repo_root/dist/axllm/axllm_c.h"

echo "axllm-c verify ok"
