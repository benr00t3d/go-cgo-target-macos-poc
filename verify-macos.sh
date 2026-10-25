#!/bin/bash
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GO_BIN=${GO_BIN:-$(command -v go 2>/dev/null || true)}
GOPLS_BIN=${GOPLS:-$(command -v gopls 2>/dev/null || true)}
PYTHON_BIN=${PYTHON:-$(command -v python3 2>/dev/null || true)}
if [ -n "${CC_BIN:-}" ]; then
    CLANG_BIN=$CC_BIN
elif command -v xcrun >/dev/null 2>&1; then
    CLANG_BIN=$(xcrun --find clang 2>/dev/null || true)
else
    CLANG_BIN=$(command -v clang 2>/dev/null || true)
fi

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 2
}

[ "$(uname -s)" = Darwin ] || fail "this candidate must run on macOS"
[ -n "$GO_BIN" ] || fail "go was not found"
[ -n "$CLANG_BIN" ] || fail "Apple Clang was not found"

MACHINE=$(uname -m)
case "$MACHINE" in
    arm64)
        GOARCH=arm64
        TARGET=arm64-cfg/poc-darwin
        CONFIG_REL=arm64-cfg/poc-darwin-clang.cfg
        ;;
    x86_64)
        GOARCH=amd64
        TARGET=x86_64-cfg/poc-darwin
        CONFIG_REL=x86_64-cfg/poc-darwin-clang.cfg
        ;;
    *)
        fail "unsupported macOS architecture: $MACHINE"
        ;;
esac

TMP=$(mktemp -d "${TMPDIR:-/tmp}/go-cgo-macos-poc.XXXXXX") || exit 2
cleanup() {
    if [ "${KEEP_TMP:-0}" = 1 ]; then
        printf 'retained=%s\n' "$TMP"
    else
        rm -rf -- "$TMP"
    fi
}
trap cleanup EXIT HUP INT TERM

cp -R "$ROOT/positive" "$TMP/positive"
cp -R "$ROOT/control" "$TMP/control"
cp -R "$ROOT/denied" "$TMP/denied"
mkdir -p "$TMP/positive/$(dirname "$CONFIG_REL")" \
    "$TMP/cache-control" "$TMP/cache-positive" "$TMP/cache-denied" \
    "$TMP/modcache" "$TMP/tmp-control" "$TMP/tmp-positive" "$TMP/tmp-denied"

DYLIB="$TMP/positive/$(dirname "$CONFIG_REL")/marker_plugin.dylib"
MARKER="$TMP/marker.txt"
"$CLANG_BIN" -dynamiclib -fPIC -arch "$MACHINE" "$ROOT/plugin/marker.c" -o "$DYLIB" \
    >"$TMP/dylib-build.stdout" 2>"$TMP/dylib-build.stderr" \
    || fail "failed to build the benign Mach-O library"

if command -v codesign >/dev/null 2>&1; then
    if codesign --force --sign - "$DYLIB" >"$TMP/codesign.stdout" 2>"$TMP/codesign.stderr"; then
        CODESIGN_STATUS=0
    else
        CODESIGN_STATUS=$?
    fi
else
    CODESIGN_STATUS=127
fi
if command -v codesign >/dev/null 2>&1; then
    codesign -dvvv "$DYLIB" >"$TMP/codesign-detail.txt" 2>&1 || true
fi
file "$DYLIB" >"$TMP/dylib.file"
rm -f "$MARKER"

# First discriminate Apple Clang itself from cmd/go. This uses the same target
# and derived config while compiling a harmless empty translation unit.
set +e
(
    cd "$TMP/positive"
    env GO_CGO_MACOS_POC_MARKER="$MARKER" \
        "$CLANG_BIN" -target "$TARGET" -c "$ROOT/probe.c" -o "$TMP/probe.o" \
        >"$TMP/direct-clang.stdout" 2>"$TMP/direct-clang.stderr"
)
DIRECT_STATUS=$?
set -e
if [ -s "$MARKER" ]; then DIRECT_MARKER=yes; else DIRECT_MARKER=no; fi

COMMON_ENV=(
    "GOENV=off"
    "GOWORK=off"
    "GOTOOLCHAIN=local"
    "GOTELEMETRY=off"
    "GOFLAGS="
    "CGO_ENABLED=1"
    "GOOS=darwin"
    "GOARCH=$GOARCH"
    "CC=$CLANG_BIN"
    "CXX=$CLANG_BIN"
    "GOMODCACHE=$TMP/modcache"
    "GO_CGO_MACOS_POC_MARKER=$MARKER"
)
env "${COMMON_ENV[@]}" "$GO_BIN" env GOOS GOARCH CGO_ENABLED CC \
    >"$TMP/go-env.txt" 2>"$TMP/go-env.stderr"

run_go_case() {
    CASE=$1
    CACHE=$2
    GOTMP=$3
    rm -f "$MARKER"
    set +e
    (
        cd "$TMP/$CASE"
        env "${COMMON_ENV[@]}" GOCACHE="$CACHE" GOTMPDIR="$GOTMP" \
            "$GO_BIN" list -compiled . >"$TMP/$CASE.stdout" 2>"$TMP/$CASE.stderr"
    )
    CASE_STATUS=$?
    set -e
    if [ -s "$MARKER" ]; then CASE_MARKER=yes; else CASE_MARKER=no; fi
}

run_go_case control "$TMP/cache-control" "$TMP/tmp-control"
CONTROL_STATUS=$CASE_STATUS
CONTROL_MARKER=$CASE_MARKER

# Seed only standard-library cache entries. Module paths differ, so fixture
# cgo results cannot be reused between cases.
cp -R "$TMP/cache-control/." "$TMP/cache-positive/" 2>/dev/null || true
cp -R "$TMP/cache-control/." "$TMP/cache-denied/" 2>/dev/null || true

run_go_case positive "$TMP/cache-positive" "$TMP/tmp-positive"
POSITIVE_STATUS=$CASE_STATUS
POSITIVE_MARKER=$CASE_MARKER

run_go_case denied "$TMP/cache-denied" "$TMP/tmp-denied"
DENIED_STATUS=$CASE_STATUS
DENIED_MARKER=$CASE_MARKER

if [ -n "$GOPLS_BIN" ] && [ -n "$PYTHON_BIN" ]; then
    mkdir -p "$TMP/cache-gopls" "$TMP/tmp-gopls"
    rm -f "$MARKER"
    set +e
    (
        cd "$TMP/positive"
        env "${COMMON_ENV[@]}" GOCACHE="$TMP/cache-gopls" GOTMPDIR="$TMP/tmp-gopls" \
            PATH="$(dirname "$GO_BIN"):$PATH" \
            "$PYTHON_BIN" "$ROOT/lsp_initialize_only.py" \
            "$TMP/positive" "$GOPLS_BIN" "$MARKER" \
            >"$TMP/gopls.stdout" 2>"$TMP/gopls.stderr"
    )
    GOPLS_STATUS=$?
    set -e
    if [ -s "$MARKER" ]; then GOPLS_MARKER=yes; else GOPLS_MARKER=no; fi
else
    GOPLS_STATUS=skip
    GOPLS_MARKER=not_run
fi

if [ "$DIRECT_MARKER" = yes ] && [ "$CONTROL_MARKER" = no ] && \
   [ "$POSITIVE_MARKER" = yes ] && [ "$DENIED_MARKER" = no ]; then
    VERDICT=APPLE_CLANG_AND_GO_PATH_POSITIVE
else
    VERDICT=NOT_PROVEN
fi

{
    printf 'macos=%s\n' "$(sw_vers -productVersion 2>/dev/null || true)"
    printf 'machine=%s\ngoarch=%s\n' "$MACHINE" "$GOARCH"
    printf 'go=%s\n' "$($GO_BIN version 2>&1)"
    printf 'clang=%s\n' "$($CLANG_BIN --version 2>&1 | head -n 1)"
    printf 'xcode=%s\n' "$(xcodebuild -version 2>/dev/null | tr '\n' ';' || true)"
    printf 'sdk=%s\n' "$(xcrun --show-sdk-path 2>/dev/null || true)"
    printf 'go_env=%s\n' "$(tr '\n' ',' <"$TMP/go-env.txt")"
    printf 'target=%s\nconfig=%s\n' "$TARGET" "$CONFIG_REL"
    printf 'dylib=%s\n' "$(cat "$TMP/dylib.file")"
    printf 'codesign_status=%s\n' "$CODESIGN_STATUS"
    printf 'direct_clang_status=%s\ndirect_clang_marker=%s\n' "$DIRECT_STATUS" "$DIRECT_MARKER"
    printf 'control_status=%s\ncontrol_marker=%s\n' "$CONTROL_STATUS" "$CONTROL_MARKER"
    printf 'positive_status=%s\npositive_marker=%s\n' "$POSITIVE_STATUS" "$POSITIVE_MARKER"
    printf 'direct_flag_status=%s\ndirect_flag_marker=%s\n' "$DENIED_STATUS" "$DENIED_MARKER"
    printf 'gopls_status=%s\ngopls_marker_before_didopen=%s\n' "$GOPLS_STATUS" "$GOPLS_MARKER"
    printf 'verdict=%s\n' "$VERDICT"
    printf 'run=%s\n' "$TMP"
} | tee "$TMP/summary.txt"

[ "$VERDICT" = APPLE_CLANG_AND_GO_PATH_POSITIVE ] || exit 1
[ "$DENIED_STATUS" -ne 0 ] || exit 1
grep -F 'invalid flag in #cgo CFLAGS: -Xclang' "$TMP/denied.stderr" >/dev/null || exit 1

if [ "$GOPLS_STATUS" != skip ] && [ "$GOPLS_MARKER" != yes ]; then
    exit 1
fi


