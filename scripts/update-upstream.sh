#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-}"

if [[ -z "$version" ]]; then
  version="$(gh release view -R ax-llm/ax --json tagName --jq .tagName)"
fi

current=""
if [[ -f "$repo_root/.ax-upstream-version" ]]; then
  current="$(cat "$repo_root/.ax-upstream-version")"
fi

if [[ "$current" == "$version" && "${AXLLM_C_FORCE_UPDATE:-}" != "1" ]]; then
  echo "ax upstream already at $version"
  exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "downloading ax-llm/ax $version"
curl -fsSL "https://github.com/ax-llm/ax/archive/refs/tags/$version.tar.gz" -o "$tmp/ax.tar.gz"
mkdir -p "$tmp/ax"
tar -xzf "$tmp/ax.tar.gz" -C "$tmp/ax" --strip-components=1

rm -rf "$repo_root/vendor/ax" "$repo_root/dist"
mkdir -p "$repo_root/vendor"
cp -a "$tmp/ax" "$repo_root/vendor/ax"

"$repo_root/scripts/apply-overlays.sh"

echo "installing upstream dependencies"
(
  cd "$repo_root/vendor/ax"
  npm ci --no-audit --no-fund
  npm run axir:generate-packages
)

mkdir -p "$repo_root/dist"
cp -a "$repo_root/vendor/ax/packages/cpp/." "$repo_root/dist/"
printf '%s\n' "$version" > "$repo_root/.ax-upstream-version"
cat > "$repo_root/upstream.json" <<JSON
{
  "owner": "ax-llm",
  "repo": "ax",
  "version": "$version",
  "source": "https://github.com/ax-llm/ax/archive/refs/tags/$version.tar.gz"
}
JSON

echo "updated ax upstream to $version"
