#!/bin/bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ "$(uname -s)" = Darwin ] || { echo "macOS is required" >&2; exit 2; }

MACHINE=$(uname -m)
case "$MACHINE" in
    arm64) CONFIG_DIR="$ROOT/arm64-cfg" ;;
    x86_64) CONFIG_DIR="$ROOT/x86_64-cfg" ;;
    *) echo "unsupported architecture: $MACHINE" >&2; exit 2 ;;
esac

CLANG_BIN=$(xcrun --find clang)
SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
DYLIB="$CONFIG_DIR/calculator_plugin.dylib"
SENTINEL="$ROOT/.calculator-demo.once"
MARKER="$ROOT/.calculator-loaded"
GOCACHE="$ROOT/.video-gocache"

mkdir -p "$CONFIG_DIR" "$ROOT/.vscode"
rm -f "$SENTINEL" "$MARKER"
rm -rf -- "$GOCACHE"

"$CLANG_BIN" -isysroot "$SDKROOT" -dynamiclib -fPIC -arch "$MACHINE" \
    "$ROOT/plugin/calculator_demo.c" -o "$DYLIB"
codesign --force --sign - "$DYLIB"

python3 - "$ROOT" "$CLANG_BIN" "$SDKROOT" "$GOCACHE" "$SENTINEL" "$MARKER" <<'PY'
import json
from pathlib import Path
import sys

root, clang, sdkroot, gocache, sentinel, marker = sys.argv[1:]
settings = {
    "go.toolsEnvVars": {
        "GOENV": "off",
        "GOWORK": "off",
        "GOTOOLCHAIN": "local",
        "GOTELEMETRY": "off",
        "GOFLAGS": "",
        "CGO_ENABLED": "1",
        "CC": clang,
        "CXX": clang,
        "SDKROOT": sdkroot,
        "GOCACHE": gocache,
        "GO_CGO_MACOS_CALC_SENTINEL": sentinel,
        "GO_CGO_MACOS_CALC_MARKER": marker,
    }
}
Path(root, ".vscode", "settings.json").write_text(
    json.dumps(settings, indent=2) + "\n", encoding="utf-8"
)
PY

touch "$ROOT/demo.go"

printf 'Prepared the VS Code Calculator demo.\n'
printf '  repo: %s\n' "$ROOT"
printf '  dylib: %s\n' "$DYLIB"
printf '  sdk: %s\n' "$SDKROOT"
printf '\nFully quit VS Code, start recording, then open this folder in VS Code.\n'
printf 'If prompted, trust the workspace and ensure the Go extension is enabled.\n'

