#!/usr/bin/env python3
"""Apply the project-owned ReXGlue SDK changes (patches/rexglue) to a fresh SDK checkout."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

# (patch, directory inside the SDK it applies to, the upstream revision that directory must be at)
PATCHES = (
    ("0001-tekken6-sdk-changes.patch", ".", "f5337cdc947ff6d4c4196737e2c807a48f2a1fc2"),
    ("0002-ffmpeg-mingw.patch", "thirdparty/FFmpeg", "0604b464c7cb4ebc94940cf1f324a3b26b87717c"),
)


def git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", "-C", str(repo), *args], check=check, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def apply(repo: Path, patch: Path, base: str) -> bool:
    """True when the patch is applied afterwards (now or already); False (with a message) when the checkout differs."""
    if not patch.is_file():
        print(f"Missing project patch: {patch}", file=sys.stderr)
        return False
    if git(repo, "apply", "--reverse", "--check", str(patch), check=False).returncode == 0:
        print(f"{patch.name}: already applied")
        return True
    revision = git(repo, "rev-parse", "HEAD").stdout.strip()
    if revision != base:
        print(f"{patch.name}: expected {repo} at {base}, found {revision}; refusing to patch a different revision.", file=sys.stderr)
        return False
    if git(repo, "status", "--porcelain", "--untracked-files=no").stdout.strip():
        print(f"{patch.name}: {repo} has local changes; refusing to overwrite them.", file=sys.stderr)
        return False
    git(repo, "apply", "--check", str(patch))
    git(repo, "apply", str(patch))
    print(f"{patch.name}: applied")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=Path(__file__).resolve().parents[1] / "third_party" / "rexglue-sdk",
                        help="ReXGlue SDK checkout (default: this repo's submodule)")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    patch_dir = Path(__file__).resolve().parents[1] / "patches" / "rexglue"
    if not (sdk / ".git").exists():
        print(f"ReXGlue SDK checkout not found: {sdk}", file=sys.stderr)
        return 2
    for name, sub, base in PATCHES:
        if not apply((sdk / sub).resolve(), patch_dir / name, base):
            return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
