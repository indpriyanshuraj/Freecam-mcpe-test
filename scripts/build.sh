#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-release}"

if [[ "$MODE" == "clean" ]]; then
    echo "== Levi Freecam clean =="
    xmake clean -y
    exit 0
fi

if [[ "$MODE" != "debug" && "$MODE" != "release" ]]; then
    echo "Usage: $0 [debug|release|clean]" >&2
    exit 2
fi
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"

if [[ -z "$NDK" ]]; then
    echo "Set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT) to an Android NDK." >&2
    exit 1
fi

export XMAKE_COLORTERM="${XMAKE_COLORTERM:-truecolor}"

echo "== Levi Freecam build =="
echo "Mode : $MODE"
echo "ABI  : arm64-v8a"
echo "NDK  : $NDK"
echo

echo "[1/3] Configuring Xmake..."
xmake f -y -p android -a arm64-v8a -m "$MODE" --ndk="$NDK"

echo "[2/3] Building native library..."
xmake -y

OUT="build/android/arm64-v8a/$MODE"
SO="$OUT/liblevi_freecam.so"
PACK="$OUT/levi_freecam.levipack"

echo "[3/3] Verifying outputs..."
test -s "$SO"
test -s "$PACK"
echo "  SO      : $SO"
echo "  Levipack: $PACK"
echo
echo "Build complete."
