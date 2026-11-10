#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v zig >/dev/null 2>&1; then
  echo "zig not installed; skipping Zig example smoke"
  exit 0
fi

sources=(dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp dist/axllm/mcp.cpp)

out="$(cd "$repo_root" && zig run examples/zig/hello.zig -I dist "${sources[@]}" -lc++ 2>&1)"
echo "$out"
grep -q 'hello from Zig + axllm-c' <<< "$out"
grep -q '"answer"' <<< "$out"

# Real HTTP inference through the built-in libcurl transport, against a local
# OpenAI-compatible mock so no API key or network access is needed.
mock_log="$(mktemp)"
python3 "$repo_root/scripts/mock-openai.py" > "$mock_log" &
mock_pid=$!
trap 'kill "$mock_pid" 2>/dev/null || true; rm -f "$mock_log"' EXIT
for _ in $(seq 50); do
  [[ -s "$mock_log" ]] && break
  sleep 0.1
done
port="$(head -n1 "$mock_log")"
[[ -n "$port" ]]

out="$(cd "$repo_root" && AXLLM_C_BASE_URL="http://127.0.0.1:$port/v1" OPENAI_API_KEY=test \
  zig run examples/zig/inference.zig -I dist -DAXLLM_ENABLE_CURL=1 "${sources[@]}" -lc++ -lcurl 2>&1)"
echo "$out"
grep -q 'zig inference via axllm-c: .*Paris' <<< "$out"
