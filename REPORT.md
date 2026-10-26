# Security report: `cmd/go` cgo `-target` validation bypass executes repository code during analysis

**Report date:** 2026-10-25  
**Affected component:** Go toolchain, `cmd/go/internal/work` cgo flag validation  
**Demonstrated platforms:** macOS/arm64 and Linux/amd64  
**Reporter:** [add preferred name and attribution]

This public repository contains a deliberately benign Calculator demonstration. It does not contain a shell payload, credential access, persistence, or network behavior.

## Summary

A Go package can place a path separator inside the operand of a source-controlled cgo `-target` option. `cmd/go` accepts the option because `-target` is allowlisted and its following token receives only the generic `SafeArg` check. That check constrains the token's first byte but does not reject later path separators.

Clang 16 and later use the selected target to derive an implicit `<target>-clang.cfg` filename. A target such as `arm64-cfg/poc-darwin` therefore makes Apple Clang read `arm64-cfg/poc-darwin-clang.cfg` relative to cgo's package working directory. The repository-controlled configuration can restore compiler options that Go intentionally rejects when they appear directly in `#cgo` directives, including `-Xclang -load`.

The result is arbitrary native code execution inside Clang while Go tooling requests compiled package metadata. The target Go program does not need to be linked or run.

On a normal Apple-silicon developer setup, this composes with VS Code's Go extension and gopls. In the recorded reproduction, opening the repository folder was sufficient in an already trusted location. In a fresh `/tmp` location, VS Code remained in Restricted Mode; selecting **Trust** caused the same execution without opening a file manually or invoking a build, test, run, task, or terminal command.

This appears to cross Go's stated security boundary: the Go Security Decisions page says that building attacker-controlled source should be safe and identifies a malicious module causing a Go toolchain command to execute arbitrary code as a vulnerability.

## Impact

The loaded library runs with the permissions and environment of the compiler process. A real payload could read developer credentials and source, modify writable files, or affect later build outputs. This proof is intentionally visible and bounded: its constructor invokes `/usr/bin/open -b com.apple.calculator`, appends a local diagnostic receipt under `/tmp`, and performs no network access or shell invocation.

The practical preconditions are:

- cgo is enabled;
- Clang with default target-configuration loading is selected;
- a tool requests compiled metadata for the attacker-controlled package; and
- for the VS Code route, the user opens and trusts the workspace, or opens it below a location already trusted by VS Code.

On the reproduced Mac, Apple Clang was Go's ordinary cgo compiler; no custom `CC` value or attacker-controlled environment variable was required. The VS Code route therefore requires user interaction, but it does not require the user to choose Build, Run, Test, a task, or a terminal command.

For conventional scoring only, my estimate is **CVSS 3.1: 8.8 High** (`CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:H/I:H/A:H`). I understand the Go project does not use CVSS and defer classification to the Go Security Team.

## Minimal visual reproduction

The `main` branch is intentionally small. On an Apple-silicon Mac with Go, Apple Command Line Tools, VS Code, and the Go extension installed:

```sh
DEMO_ROOT="$(mktemp -d /tmp/go-cgo-calculator.XXXXXX)"
gh repo clone benr00t3d/go-cgo-target-macos-poc "$DEMO_ROOT/repo"
code --new-window "$DEMO_ROOT/repo"
```

Expected behavior:

1. A fresh `/tmp` workspace opens in Restricted Mode.
2. Select **Trust** for this workspace.
3. VS Code selects the repository's `README.go`, the Go extension starts gopls, and Calculator opens without another action.

If the clone is placed below a path already trusted by VS Code, Calculator may open immediately when the folder opens. A fresh clone path is recommended because VS Code and Go caches can suppress repeated compiler work for an unchanged checkout.

The video recorded on 2026-10-25 shows both cases:

- opening a clone under an already trusted Downloads location caused Calculator to open automatically; and
- opening a fresh clone under `/tmp` first showed Restricted Mode, then Calculator opened immediately after Workspace Trust was granted.

**Video:** [add private video link before sending]

## What each PoC file does

| File | Purpose |
|---|---|
| `README.go` | Root cgo package with `#cgo darwin,arm64 CFLAGS: -target arm64-cfg/poc-darwin` |
| `arm64-cfg/poc-darwin-clang.cfg` | Target-derived Clang configuration containing `-Xclang -load -Xclang <CFGDIR>/calculator_plugin.dylib` |
| `arm64-cfg/calculator_plugin.dylib` | Ad-hoc-signed arm64 Mach-O demonstration library |
| `plugin/calculator_demo.c` | Exact source corresponding to the demonstration library |
| `.vscode/settings.json` | Selects VS Code's `readme` startup editor so the root `README.go` activates the Go extension |
| `go.mod` | Makes the checkout a Go workspace root |
| `SHA256SUMS` | Verifies the six PoC inputs and library source |

The human report is named `REPORT.md`, not `README.md`, because a root `README.md` takes precedence over `README.go` in the observed VS Code startup-file selection.

## Execution chain

```text
attacker-controlled Go workspace or module
  -> #cgo accepts: -target arm64-cfg/poc-darwin
  -> cmd/go runs cgo/Clang from the package directory
  -> Clang derives arm64-cfg/poc-darwin-clang.cfg
  -> the config restores the otherwise denied -Xclang -load option
  -> Clang maps the repository dylib
  -> the dylib constructor runs as the analyzing user
```

For the visual path, VS Code supplies a natural analyzer trigger:

```text
open/trust folder
  -> VS Code opens README.go as the configured startup document
  -> Go extension activates and starts gopls
  -> gopls requests compiled package metadata
  -> cmd/go invokes cgo and Apple Clang
  -> constructor opens Calculator
```

The Go extension and VS Code are not the root cause; they demonstrate that ordinary analysis reaches the vulnerable Go toolchain path.

## Matched controls and independent evidence

The safe verifier retained at tag `macos-live-positive-20261025` uses a marker-only library and three matched cases. The live M1 result was:

```text
macos=15.4.1
machine=arm64
go=go version go1.26.2 darwin/arm64
clang=Apple clang version 17.0.0 (clang-1700.0.13.3)
gopls=golang.org/x/tools/gopls v0.21.1
direct_clang_marker=yes
control_status=0
control_marker=no
positive_status=0
positive_marker=yes
direct_flag_status=1
direct_flag_marker=no
gopls_status=0
gopls_marker_before_didopen=yes
verdict=APPLE_CLANG_AND_GO_PATH_POSITIVE
```

These controls distinguish the issue from expected execution of cgo output:

- direct Apple Clang invocation proved that the derived target configuration loads the library;
- the same cgo directive without the derived configuration remained marker-negative;
- `go list -compiled` loaded the library and exited successfully;
- placing `-Xclang -load` directly in `#cgo CFLAGS` was rejected by Go; and
- the LSP client observed the marker during initialization before it sent any `textDocument/didOpen` notification.

The video-tested minimal-repository revision is `38ce6f0cbbd86bb9e33269215f78122c11036d86`. The later reporting commit adds documentation and the already-used plugin source; it does not change the five trigger inputs from that recorded revision.

Additional behavioral testing reached the same constructor during source or metadata analysis with official `govulncheck` v1.9.0, gopls v0.24.0, Google OSV-Scanner v2.7.0, Google Capslock, Google Kythe extraction, and `golangci-lint` v2.14.0. These results show that the primitive is not specific to VS Code, but this report does not claim a public unauthenticated Google production compromise.

## Tested version matrix

| Component | Result |
|---|---|
| Go 1.26.2, macOS/arm64 | Positive |
| Go 1.26.10, Linux/amd64 | Positive |
| Go 1.27.3, Linux/amd64 | Positive |
| Go tip `0d37007fda071631fbcb520b99c85a8e4c93108a`, tested 2026-10-25 | Positive |
| Apple Clang 17.0.0 (`clang-1700.0.13.3`) | Positive |
| Upstream Clang 16.0.6, 18.1.8, and 22.1.8 | Positive |
| Upstream Clang 10.0.1, 14.0.6, and 15.0.7 | Negative |
| gopls v0.21.1 on macOS | Positive during initialization before `didOpen` |
| gopls v0.24.0 on Linux | Positive during initialization before `didOpen` |
| VS Code 1.143.1, Go extension v0.58.0 | Positive after folder trust; positive immediately below an already trusted location |

The table claims only versions that were directly exercised.

## Root cause in Go

At the tested Go 1.27.3 revision (`2aae1000ea1d4b28923b96db28acf4e3b99d6aff`):

1. `-target` is accepted as a compiler and linker option that consumes the next argument in [`cmd/go/internal/work/security.go`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/security.go#L141-L255).
2. [`checkFlags`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/security.go#L366-L468) applies the generic `SafeArg` predicate to the operand, then accepts the pair without option-specific validation.
3. [`load.SafeArg`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/load/pkg.go#L2671-L2681) checks the first byte. An alphanumeric-leading value containing a later `/` therefore passes.
4. [`runCgo`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/exec.go#L2947-L3094) runs from the package directory and forwards the accepted vector to the compiler.

Clang's documented configuration behavior then supplies the second half of the chain: the target affects the implicit `<triple>-<driver>.cfg` name, configuration files contain compiler options, and `<CFGDIR>` expands relative to the configuration file. The filesystem interpretation is expected Clang behavior; the Go-specific flaw is assigning filesystem semantics to a package-controlled `-target` operand after classifying it as safe.

## Why this is unexpected code execution

Go already rejects the equivalent direct native-load flags from `#cgo`, and the negative control proves that protection is active. The bypass makes a repository file alter the compiler's own process and execute a dynamic-library constructor before any target program exists.

The Go Security Decisions page states that building malicious source should be safe and specifically lists a malicious module causing `go build` to execute arbitrary code as a vulnerability. This reproduction reaches the same compiler boundary through `go list -compiled` and analyzer-driven metadata loading; it is not `go test` running attacker tests or a user launching a produced executable.

## Suggested fix

Apply option-specific validation to package-originated `-target` operands in both compiler and linker flag checks. After the existing `SafeArg` check, reject path semantics such as `/`, `\\`, and `:`. Normal path-free target triples can remain supported.

The simpler maximum-assurance alternative is to remove `-target` from the package-controlled allowlists while continuing to allow trusted users to select target options through their environment.

Suggested regression coverage:

- reject slash-, backslash-, and colon-bearing `-target` operands from `#cgo` directives;
- retain a path-free target triple if compatibility requires it;
- test both compiler and linker next-argument tables; and
- include an end-to-end cgo case proving rejection before the compiler process loads a package-relative config.

Passing `--no-default-config` or setting `CLANG_NO_DEFAULT_CONFIG=1` would be useful defense in depth, but it changes compiler behavior and should not replace validation of package-originated input.

## Reproduction integrity and source

The five trigger inputs at the video-tested revision have these SHA-256 hashes:

```text
d0b6d5d74faafaa588e3070322b67150e8beaee633e5bc13d3f2dc89ef716649  .vscode/settings.json
1d703642525567bea5cff27cadf2d0d414e503d2dfbc44fe992944ec6383cdb4  README.go
8dc6f589d902cbb23660f6865fb819068092450f39bcc7c95c776cf50501dd08  go.mod
1404394e3e32913842cf271e672bf2f8aef301f61f3f70d1c6ec2d39f01e8300  arm64-cfg/poc-darwin-clang.cfg
9e3c886aec3e2848a30d670b9793531b1866fe5c902b838111dc3788f063ca26  arm64-cfg/calculator_plugin.dylib
```

The exact library source in `plugin/calculator_demo.c` has SHA-256 `11cd73330db56c0c034fd642eb5cb88b4197f187199106b810b0469c450ab3bc`. The shipped binary was built and tested by [successful macOS 15 arm64 workflow run 40234663019](https://github.com/benr00t3d/go-cgo-target-macos-poc/actions/runs/40234663019) from source revision `5e7687349eed5fcf1221b742d9aae650b3c2291e`, then committed unchanged. The essential build commands were:

```sh
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
CLANG="$(xcrun --find clang)"
"$CLANG" -isysroot "$SDKROOT" -dynamiclib -fPIC -arch arm64 \
  plugin/calculator_demo.c -o arm64-cfg/calculator_plugin.dylib
codesign --force --sign - arm64-cfg/calculator_plugin.dylib
```

The prebuilt binary is included only to make the recorded folder-open behavior reproducible without asking the reviewer to run a preparation script.

## References

- [Go Security Decisions: Remote Code Execution and Building malicious code](https://go.dev/doc/security/decisions)
- [Go Security Policy](https://go.dev/doc/security/policy)
- [Clang configuration files](https://clang.llvm.org/docs/UsersManual.html#configuration-files)
- [CVE-2018-6574 / GO-2022-0201: command execution through cgo compiler options](https://pkg.go.dev/vuln/GO-2022-0201)
- [CVE-2023-29404 / GO-2023-1841: cgo option/operand validation issue](https://pkg.go.dev/vuln/GO-2023-1841)

