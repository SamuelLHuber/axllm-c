#!/usr/bin/env bash
set -euo pipefail

version="${ZIG_VERSION:-0.17.0}"
install_root="${ZIG_INSTALL_ROOT:-/tmp/axllm-c-zig}"
os="$(uname -s)"
arch="$(uname -m)"

case "$os:$arch" in
  Linux:x86_64)
    name="zig-x86_64-linux-$version"
    sha256="1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026"
    ;;
  Linux:aarch64|Linux:arm64)
    name="zig-aarch64-linux-$version"
    sha256="9e8d11661d4ae3bd57702a3832781e23ad151dde5798e16a5ccd503f65234ff8"
    ;;
  Darwin:x86_64)
    name="zig-x86_64-macos-$version"
    sha256="4f9a1c5269aa17ebda5e6d3c2b89d6cbf36f7d2b22a0306e9ab98f25f95529c6"
    ;;
  Darwin:arm64|Darwin:aarch64)
    name="zig-aarch64-macos-$version"
    sha256="b607e9b9234790a008116ae5bdb71c6243b84b9fb42a53a9e70fde41c06c536a"
    ;;
  *)
    echo "unsupported Zig host: $os $arch" >&2
    exit 1
    ;;
esac

zig_bin="$install_root/$name/zig"
if [[ -x "$zig_bin" ]]; then
  "$zig_bin" version
  echo "$install_root/$name" >> "${GITHUB_PATH:-/dev/null}"
  exit 0
fi

mkdir -p "$install_root"
tarball="$install_root/$name.tar.xz"
url="https://ziglang.org/download/$version/$name.tar.xz"

curl -fsSLo "$tarball" "$url"
echo "$sha256  $tarball" | shasum -a 256 -c -
tar -xf "$tarball" -C "$install_root"
"$zig_bin" version

if [[ -n "${GITHUB_PATH:-}" ]]; then
  echo "$install_root/$name" >> "$GITHUB_PATH"
else
  echo "add to PATH: $install_root/$name"
fi
