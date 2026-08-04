#!/usr/bin/env python3
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "scripts" / "install-zig.sh"
INDEX_URL = "https://ziglang.org/download/index.json"
TARGETS = {
    "x86_64-linux": "Linux:x86_64",
    "aarch64-linux": "Linux:aarch64|Linux:arm64",
    "x86_64-macos": "Darwin:x86_64",
    "aarch64-macos": "Darwin:arm64|Darwin:aarch64",
}


def main() -> int:
    requested = sys.argv[1] if len(sys.argv) > 1 else ""
    with urllib.request.urlopen(INDEX_URL, timeout=30) as response:
        index = json.load(response)

    version = requested or latest_stable(index)
    release = index.get(version)
    if not isinstance(release, dict):
        raise SystemExit(f"Zig version {version!r} not found in {INDEX_URL}")

    text = INSTALLER.read_text()
    current = re.search(r'version="\$\{ZIG_VERSION:-([^}]+)\}"', text)
    if not current:
        raise SystemExit("could not find default Zig version in scripts/install-zig.sh")

    text = re.sub(
        r'version="\$\{ZIG_VERSION:-[^}]+\}"',
        f'version="${{ZIG_VERSION:-{version}}}"',
        text,
        count=1,
    )

    for platform, case_label in TARGETS.items():
        shasum = release.get(platform, {}).get("shasum")
        if not shasum:
            raise SystemExit(f"missing shasum for Zig {version} {platform}")
        pattern = rf'({re.escape(case_label)}\)\n\s+name="zig-[^"\n]+-\$version"\n\s+sha256=")[a-f0-9]+("\n\s+;;)'
        text, count = re.subn(pattern, rf'\g<1>{shasum}\2', text, count=1)
        if count != 1:
            raise SystemExit(f"could not update {platform} hash in scripts/install-zig.sh")

    INSTALLER.write_text(text)
    print(version)
    return 0


def latest_stable(index: dict) -> str:
    versions = [key for key in index if re.fullmatch(r"\d+\.\d+\.\d+", key)]
    if not versions:
        raise SystemExit("no stable Zig releases found")
    return max(versions, key=lambda value: tuple(map(int, value.split("."))))


if __name__ == "__main__":
    raise SystemExit(main())
