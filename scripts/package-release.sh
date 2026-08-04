#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-}"
if [[ -z "$version" ]]; then
  version="$(cat "$repo_root/.ax-upstream-version")"
fi

out_dir="${2:-$repo_root/.release}"
mkdir -p "$out_dir"
asset="$out_dir/axllm-c-$version-dist.tar.gz"
rm -f "$asset"

tar -C "$repo_root/dist" -czf "$asset" .
sha256="$(shasum -a 256 "$asset" | awk '{print $1}')"
printf '%s  %s\n' "$sha256" "$(basename "$asset")" > "$asset.sha256"

echo "$asset"
