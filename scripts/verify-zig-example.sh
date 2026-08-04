#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v zig >/dev/null 2>&1; then
  echo "zig not installed; skipping Zig example smoke"
  exit 0
fi

out="$(cd "$repo_root" && zig run examples/zig/hello.zig -I dist dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp dist/axllm/mcp.cpp -lc++ 2>&1)"
echo "$out"
grep -q 'hello from Zig + axllm-c' <<< "$out"
grep -q '"answer"' <<< "$out"
