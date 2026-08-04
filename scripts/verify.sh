#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${AXLLM_C_BUILD_DIR:-/tmp/axllm-c-build}"

if [[ ! -d "$repo_root/dist" ]]; then
  echo "missing dist; run scripts/update-upstream.sh first" >&2
  exit 1
fi

test -f "$repo_root/dist/axllm/axllm_c.h"
test -f "$repo_root/dist/axllm/axllm_c.cpp"
grep -q 'extern "C"' "$repo_root/dist/axllm/axllm_c.h"
grep -q 'axllm_c_schema_for_signature' "$repo_root/dist/axllm/axllm_c.h"
! grep -Eq 'std::|namespace|class |template<|#include <string>|#include <vector>' "$repo_root/dist/axllm/axllm_c.h"
grep -q 'axllm/axllm_c.cpp' "$repo_root/dist/CMakeLists.txt"
grep -q 'c_signature_schema' "$repo_root/dist/CMakeLists.txt"
grep -Fq 'PATTERN "*.h"' "$repo_root/dist/CMakeLists.txt"
grep -q "cpp-c-abi-signature-schema-ok" "$repo_root/dist/examples/c_signature_schema.cpp"
grep -q "Apache License" "$repo_root/LICENSE"
grep -q "Apache License" "$repo_root/dist/LICENSE"
grep -q "Apache License" "$repo_root/vendor/ax/LICENSE"

rm -rf "$build_dir"
cmake -S "$repo_root/dist" -B "$build_dir" -DAX_BUILD_EXAMPLES=ON -DAX_BUILD_CONFORMANCE=OFF
cmake --build "$build_dir" --target signature_schema c_signature_schema
"$build_dir/signature_schema"
"$build_dir/c_signature_schema"

echo "axllm-c verify ok"
