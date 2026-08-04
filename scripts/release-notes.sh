#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-$(cat "$repo_root/.ax-upstream-version") }"
version="${version//[[:space:]]/}"
cat <<EOF
# axllm-c $version

Generated C ABI facade for Ax $version.

## Contents

- Vendored upstream Ax release: $version
- Generated C++ package in \`dist/\`
- C ABI header: \`dist/axllm/axllm_c.h\`
- C ABI source: \`dist/axllm/axllm_c.cpp\`
- Zig smoke example: \`examples/zig/hello.zig\`

## Verified

- C ABI header stays C-compatible.
- CMake builds the generated package.
- \`signature_schema\` smoke passes.
- \`c_signature_schema\` C ABI smoke passes.
- Zig example compiles and calls \`axllm_c_schema_for_signature\`.

## License

Ax is Apache-2.0. Upstream and generated license files are preserved.
EOF
