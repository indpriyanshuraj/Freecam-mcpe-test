#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-release}"
export COLORTERM="${COLORTERM:-truecolor}"
export TERM="${TERM:-xterm-256color}"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"

if [[ -z "$NDK" ]]; then
    echo "Set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT) to an Android NDK." >&2
    exit 1
fi

xmake f -y -p android -a arm64-v8a -m "$MODE" --ndk="$NDK"
xmake -y
