#!/usr/bin/env bash
set -euo pipefail

LEVEL="${1:-V}"
case "$LEVEL" in
  V|D|I|W|E|F) ;;
  *)
    echo "Usage: $0 [V|D|I|W|E|F]" >&2
    exit 2
    ;;
esac

exec adb logcat -v threadtime -s "LeviFreecam:${LEVEL}" '*:S'
