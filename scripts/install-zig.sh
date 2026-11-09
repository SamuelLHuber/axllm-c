#!/usr/bin/env bash
set -euo pipefail

version="${ZIG_VERSION:-0.17.1}"
install_root="${ZIG_INSTALL_ROOT:-/tmp/axllm-c-zig}"
os="$(uname -s)"
arch="$(uname -m)"

case "$os:$arch" in
  Linux:x86_64)
    name="zig-x86_64-linux-$version"
    sha256="3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a"
    ;;
  Linux:aarch64|Linux:arm64)
    name="zig-aarch64-linux-$version"
    sha256="a1546d5646eb184437918d6635ca6b762a0e889eac09cef720ee7a1ed3fe7795"
    ;;
  Darwin:x86_64)
    name="zig-x86_64-macos-$version"
    sha256="9af07b20319fbd0c92ee46ad23e81d6a782b1aaea2cfb8dcbff35f1d2c7ddbb7"
    ;;
  Darwin:arm64|Darwin:aarch64)
    name="zig-aarch64-macos-$version"
    sha256="75f1bc7a1e9852f17ec9a0c1895f767c4ec56d46bc8a834920eece1d0359dd8d"
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
