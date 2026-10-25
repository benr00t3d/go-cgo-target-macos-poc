# VS Code Calculator demo and macOS cgo verifier

## Visual demo: open the folder, Calculator appears

This private repository can demonstrate the impact visually on an owned Mac.
The preparation step builds an ad-hoc-signed native library and writes local
VS Code Go-tool environment settings. It does **not** load the library.

Before recording, close VS Code and run once:

```sh
./prepare-vscode-demo.sh
```

Then fully quit VS Code, start recording, and open this repository folder in
VS Code. With the Go extension enabled and the workspace trusted, gopls loads
compiled package metadata during workspace initialization. Apple Clang reads
the repository target config, loads the dylib, and the dylib opens Calculator.
No build, test, or Go program is started by the user.

For another take, quit VS Code and run `./prepare-vscode-demo.sh` again. It
removes only this repository's demo sentinel and private Go build cache.

The visual plugin launches Calculator by its Apple bundle identifier through
`/usr/bin/open` directly with
`posix_spawn`; it does not invoke a shell or access the network. The marker-only
vendor verifier remains available under tag `vendor-marker-only-20261025`.

## Marker-only verifier

Status: **confirmed on an owned Apple-silicon Mac**. A live run on macOS
15.4.1 arm64 with Go 1.26.2 and Apple Clang 17.0.0 passed the direct-driver,
matched control, positive Go, direct-flag denial, and pre-`didOpen` gopls
gates. This directory remains a safe, self-contained verifier for additional
versions.

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

`GO_BIN` and `CC_BIN` can select exact binaries. Use `KEEP_TMP=1` to retain the
temporary directory, `summary.txt`, per-case logs, `receipt-sha256.txt`, and a
small `macos-cgo-receipt.tar.gz` suitable for returning to the researcher.

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

The verifier automatically bundles `summary.txt`, `go-env.txt`, all three
stderr/stdout pairs, `direct-clang.stderr`, separate positive marker snapshots,
and `file`/code-signing output. `receipt-sha256.txt` pins the summary, markers,
configuration, and generated dylib. A macOS receipt must show:

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

