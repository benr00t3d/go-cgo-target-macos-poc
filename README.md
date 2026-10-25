# macOS candidate: cgo `-target` / Apple Clang configuration loading

Status: **prepared but not executed on macOS**. This directory is a safe,
self-contained discriminator for an owned Mac. A Linux syntax review cannot
establish that Apple Clang actually follows the same implicit configuration
path or that dyld accepts the library.

The candidate does not launch a shell, access the network, or run a produced
Go program. Its Mach-O dynamic library has one constructor that appends a line
to a marker inside the verifier's temporary directory.

## Requirements

- an owned macOS host (`arm64` or `x86_64`);
- Xcode Command Line Tools (`xcrun clang`);
- a native Go installation with cgo enabled;
- optional gopls and Python 3 for the initialization-before-`didOpen` check.

## Run on the Mac

```sh
chmod +x verify-macos.sh
./verify-macos.sh
```

`GO_BIN` and `CC_BIN` can select exact binaries. `KEEP_TMP=1` retains the
temporary directory and logs.

The verifier resolves the active macOS SDK with `xcrun` and exports
`SDKROOT` explicitly. This is needed on Command Line Tools installations where
invoking the resolved Clang binary directly does not otherwise find SDK
headers.

The script prints Apple Clang, macOS, Go, architecture, code-signing, direct
driver, and three Go decisions. The decisive vulnerable result is:

```text
direct_clang_marker=yes
control_marker=no
positive_marker=yes
direct_flag_marker=no
gopls_marker_before_didopen=yes
verdict=APPLE_CLANG_AND_GO_PATH_POSITIVE
```

`positive_status=0` is the cleanest result. If the marker is present but the
command later fails, code execution is still observed, but the receipt must
describe the later compiler/linker error rather than claiming a fully
successful `go list`.

When gopls is on `PATH` (or selected with `GOPLS`), the verifier also starts a
workspace session, sends only `initialize` and `initialized`, and requires the
marker before shutdown. It never sends `textDocument/didOpen`. If gopls is not
available, that row is reported as `not_run`; the core Go/Apple Clang checks
still run.

## What the verifier changes per architecture

The Go source contains architecture-scoped directives:

- `arm64`: `-target arm64-cfg/poc-darwin`
- `amd64`: `-target x86_64-cfg/poc-darwin`

The verifier builds a native `marker_plugin.dylib`, places it beside the
matching derived configuration, and ad-hoc signs it when `codesign` is
available. The configuration uses `<CFGDIR>` so it contains no verifier- or
machine-specific absolute path.

## Proof gates

A macOS receipt should retain `summary.txt`, `go-env.txt`, all three
stderr/stdout pairs, `direct-clang.stderr`, the marker, `file` and code-signing
output for the dylib, and hashes of this directory. It must show:

1. the library is Mach-O for the running architecture;
2. direct Apple Clang invocation loads it through the derived target config;
3. the identical target with no config is marker-negative;
4. `go list -compiled` loads it in the positive fixture;
5. Go rejects the directly written `-Xclang -load` control; and
6. no target Go program is run.

Only after those gates pass should the Go report claim macOS support. A second
run through gopls workspace initialization can then extend the already-proven
Linux impact trigger to macOS.

## Likely negative-result causes

- Apple Clang may omit or alter upstream default-config lookup.
- Its derived filename may differ from `<target>-clang.cfg`.
- Apple library validation or signing policy may reject the local dylib.
- The crafted Darwin triple may load the config but fail later SDK/platform
  checks; the marker still distinguishes pre-error execution.
- An environment may set `CLANG_NO_DEFAULT_CONFIG` or disable cgo.

