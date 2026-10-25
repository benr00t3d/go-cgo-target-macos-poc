# VS Code Calculator demo and macOS cgo verifier

## Visual demo: open `demo.go`, Calculator appears

This private repository can demonstrate the impact visually on an owned Mac.
The `main` branch already contains the ad-hoc-signed arm64 Mach-O library,
target-derived Clang configuration, and cgo package. There is no victim-side
preparation command.

The victim flow is only:

1. Clone the private repository to a fresh path on an Apple-silicon Mac.
2. Open that folder normally in VS Code.
3. If prompted, trust the workspace and ensure the Go extension is enabled.
4. Click `demo.go` in the Explorer.

VS Code lazily activates the Go extension when the Go file is opened. During
the resulting gopls workspace initialization, gopls requests compiled package
metadata. Apple Clang reads the repository target config, loads the included
dylib, and the dylib opens Calculator. The victim does not run a setup script,
build, test, or Go program.

The direct LSP reproduction still shows that, once gopls is running, execution
occurs during initialization before a `textDocument/didOpen` notification is
needed. The additional click is the measured VS Code extension-activation
boundary, not a compiler or gopls requirement.

Use `gh repo clone` rather than a browser ZIP for the recording so macOS does
not add browser-download quarantine metadata to the dylib. The included binary
is arm64 and was built and signed on GitHub's macOS runner. SHA-256:
`2ffe15e1763bddd6c519bf72ccddf0564cb2b223d9a4a8bcf0b9371794a0ffe5`.

For another recording take, use a new clone path and remove the one-shot file
`/tmp/go-cgo-vscode-calculator.once` before reopening VS Code. This reset is
only for the researcher making repeated recordings; it is not part of the
victim flow.

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

Those gates passed on the retained M1 receipt. The marker-only evidence remains
the submission proof; the root-module Calculator path is an optional visual
demonstration of the same compiler load.

## Likely negative-result causes

- Apple Clang may omit or alter upstream default-config lookup.
- Its derived filename may differ from `<target>-clang.cfg`.
- Apple library validation or signing policy may reject the local dylib.
- The crafted Darwin triple may load the config but fail later SDK/platform
  checks; the marker still distinguishes pre-error execution.
- An environment may set `CLANG_NO_DEFAULT_CONFIG` or disable cgo.

