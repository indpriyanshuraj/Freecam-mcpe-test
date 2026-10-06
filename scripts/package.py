#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a Levi native mod as a .levipack")
    parser.add_argument("--library", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--mod-id", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    library = Path(args.library).resolve()
    manifest = Path(args.manifest).resolve()
    output = Path(args.output).resolve()

    if not library.is_file():
        raise SystemExit(f"library not found: {library}")
    if not manifest.is_file():
        raise SystemExit(f"manifest not found: {manifest}")
    if not args.mod_id or "/" in args.mod_id or "\\" in args.mod_id or ".." in args.mod_id:
        raise SystemExit("invalid mod id")

    data = json.loads(manifest.read_text(encoding="utf-8"))
    entry = data.get("entry")
    if not isinstance(entry, str) or not entry:
        raise SystemExit("manifest entry is missing")

    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, "w", ZIP_DEFLATED) as package:
        package.write(library, f"{args.mod_id}/{entry}")
        package.write(manifest, f"{args.mod_id}/manifest.json")

    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
