#!/usr/bin/env python3
"""Verify the documented 1.26.52.3 Build ID using the system readelf tool."""
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

EXPECTED_BUILD_ID = "56de9eed077631e03a31f4f58eb2f0e00071d335"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    if not args.library.is_file():
        raise SystemExit(f"not found: {args.library}")

    try:
        out = subprocess.check_output(["readelf", "-n", str(args.library)], text=True, stderr=subprocess.STDOUT)
    except FileNotFoundError:
        raise SystemExit("readelf is required")
    except subprocess.CalledProcessError as exc:
        print(exc.output)
        raise SystemExit(exc.returncode)

    match = re.search(r"Build ID:\s*([0-9a-fA-F]+)", out)
    actual = match.group(1).lower() if match else None
    print(f"Build ID: {actual or '<missing>'}")
    if actual != EXPECTED_BUILD_ID:
        raise SystemExit(f"Build ID mismatch: expected {EXPECTED_BUILD_ID}")
    print("Target layout gate: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
