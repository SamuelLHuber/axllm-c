#!/usr/bin/env bash
set -euo pipefail

version="${ZIG_VERSION:-0.16.0}"
install_root="${ZIG_INSTALL_ROOT:-/tmp/axllm-c-zig}"
os="$(uname -s)"
arch="$(uname -m)"

case "$os:$arch" in
  Linux:x86_64)
    name="zig-x86_64-linux-$version"
    sha256="70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00"
    ;;
  Linux:aarch64|Linux:arm64)
    name="zig-aarch64-linux-$version"
    sha256="ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17"
    ;;
  Darwin:x86_64)
    name="zig-x86_64-macos-$version"
    sha256="0387557ed1877bc6a2e1802c8391953baddba76081876301c522f52977b52ba7"
    ;;
  Darwin:arm64|Darwin:aarch64)
    name="zig-aarch64-macos-$version"
    sha256="b23d70deaa879b5c2d486ed3316f7eaa53e84acf6fc9cc747de152450d401489"
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
