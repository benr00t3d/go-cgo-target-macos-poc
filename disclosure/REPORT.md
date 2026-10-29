# cmd/go cgo `-target` validation bypass loads repository code during analysis

Date: 2026-10-25

Component: `cmd/go` cgo flag validation

Reporter: Robert Prast

Repository: https://github.com/benr00t3d/go-cgo-target-macos-poc

## Summary

I found a way for a Go package to make `cmd/go` load a package-controlled Clang configuration file. That file can tell Clang to load a dynamic library from the package. Its constructor runs while Go tooling is inspecting the package. The target Go program is never run.

The package supplies this directive:

```go
/*
#cgo darwin,arm64 CFLAGS: -target arm64-cfg/poc-darwin
*/
import "C"
```

Go accepts `-target` and checks its operand with `SafeArg`. That check only constrains the first byte, so the slash is accepted. Clang uses the target value to derive this filename relative to the package directory:

```text
arm64-cfg/poc-darwin-clang.cfg
```

The repository supplies that file with:

```text
-Xclang -load -Xclang <CFGDIR>/calculator_plugin.dylib
```

Go rejects the same `-Xclang -load` pair when it appears directly in a `#cgo` directive. Passing it through Clang's target-derived configuration bypasses that check.

## Impact

The library runs inside Clang with the permissions of the person or service analyzing the package. It could read credentials and source code, modify files, or interfere with later builds.

The included library only opens Calculator and writes a diagnostic receipt under `/tmp`. It does not start a shell or make network connections.

The conditions are:

- cgo is enabled;
- Clang supports target-derived default configuration files;
- a tool requests compiled package metadata; and
- for the VS Code path, the workspace is trusted or the user grants Workspace Trust.

On macOS, the reproduced setup used the normal Apple Clang selected by Go. No custom `CC` setting was needed.

### Why the VS Code demo matters

The bug is in Go, not VS Code. VS Code shows how it can be reached through a normal developer action: opening a cloned repository with Go language support enabled.

[VS Code trust applies to every subfolder of a trusted parent](https://code.visualstudio.com/docs/editing/workspaces/workspace-trust#_selecting-a-parent-folder). Developers often keep many repositories below one source directory, so they do not have to approve each checkout separately. A clone below a trusted `src`, Downloads, `/tmp`, or other parent can therefore start gopls with no new prompt. That is what happened with Downloads in the first part of the video.

The second part shows the protected case. A fresh `/tmp` clone stayed in Restricted Mode. Once Trust was granted, gopls started its normal workspace analysis and the library loaded immediately. The user did not choose to build or run the repository. This is the real-world boundary crossed by the Go bug: trusting a workspace for editor features also allowed package input to execute native code during background analysis.

## Reproduction

On an Apple-silicon Mac with Go, Xcode Command Line Tools, VS Code, and the Go extension installed:

```sh
DEMO_ROOT="$(mktemp -d /tmp/go-cgo-calculator.XXXXXX)"
gh repo clone benr00t3d/go-cgo-target-macos-poc "$DEMO_ROOT/repo"
code --new-window "$DEMO_ROOT/repo"
```

A fresh `/tmp` clone opens in Restricted Mode. After Workspace Trust is granted, VS Code opens `README.go`, the Go extension starts gopls, and Calculator opens. There is no need to click a Go file or run a build, test, task, or terminal command.

If the repository is opened below a location that VS Code already trusts, Calculator opens when the folder opens. A fresh clone path avoids reuse of earlier workspace and build caches.

The video recorded on 2026-10-25 shows both cases:

1. A clone under an already trusted Downloads directory opens Calculator immediately.
2. A fresh `/tmp` clone starts in Restricted Mode, then opens Calculator as soon as Trust is granted.

Video: [private video URL]

## PoC layout

| File | Role |
|---|---|
| `README.go` | cgo package containing the crafted `-target` operand |
| `arm64-cfg/poc-darwin-clang.cfg` | Clang configuration derived from that operand |
| `arm64-cfg/calculator_plugin.dylib` | arm64 demonstration library |
| `plugin/calculator_demo.c` | source for the demonstration library |
| `.vscode/settings.json` | asks VS Code to open the root `README.go` at startup |
| `SHA256SUMS` | hashes for the PoC inputs and library source |

VS Code is only the visible trigger in this demonstration. The underlying path is `gopls -> cmd/go -> cgo -> Clang`.

## Results

The video-tested repository revision is `38ce6f0cbbd86bb9e33269215f78122c11036d86`. Later commits only add documentation, checksums, and the source already used to build the dylib.

The Mac used for the controlled test had:

```text
macOS 15.4.1 arm64
Go 1.26.2 darwin/arm64
Apple Clang 17.0.0 (clang-1700.0.13.3)
gopls v0.21.1
VS Code 1.143.1
Go extension v0.58.0
```

The marker-only verifier at tag `macos-live-positive-20261025` produced:

```text
direct_clang_marker=yes
control_marker=no
positive_status=0
positive_marker=yes
direct_flag_status=1
direct_flag_marker=no
gopls_status=0
gopls_marker_before_didopen=yes
```

The controls show that:

- Apple Clang loaded the dylib through the derived target configuration;
- the same cgo directive without that configuration did not load it;
- `go list -compiled` loaded it and exited successfully;
- Go rejected the direct `-Xclang -load` form; and
- gopls loaded it during initialization before the test client sent `textDocument/didOpen`.

I also reproduced the core path on Linux with Go 1.26.10, Go 1.27.3, and Go tip. Clang 16.0.6, 18.1.8, and 22.1.8 were positive. Clang 10.0.1, 14.0.6, and 15.0.7 were negative.

## Root cause

At the tested Go 1.27.3 revision, `2aae1000ea1d4b28923b96db28acf4e3b99d6aff`:

1. [`security.go`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/security.go#L141-L255) allows `-target` as an option that consumes the next argument.
2. [`checkFlags`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/security.go#L366-L468) applies the generic `SafeArg` check to that argument and has no `-target`-specific validation.
3. [`SafeArg`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/load/pkg.go#L2671-L2681) checks the first byte, so an alphanumeric value containing a later slash passes.
4. [`runCgo`](https://github.com/golang/go/blob/2aae1000ea1d4b28923b96db28acf4e3b99d6aff/src/cmd/go/internal/work/exec.go#L2947-L3094) invokes the compiler from the package directory with the accepted arguments.

Clang then derives `<target>-clang.cfg`, reads the package-relative file, expands `<CFGDIR>`, and processes the options inside it. This is documented Clang behavior. The issue is that Go gives a package-controlled `-target` operand filesystem meaning after accepting it as safe.

## Suggested fix

Add specific validation for package-provided `-target` operands in both the compiler and linker flag checks. Reject path separators and other path syntax such as `/`, `\\`, and `:`. Path-free target triples can remain valid.

Removing `-target` from the package-controlled allowlists would also close the path, but may have a larger compatibility cost.

Regression tests should cover both flag tables, reject path-bearing operands before the compiler starts, and retain a normal target triple if support is required.

## Integrity

Run this from the repository root:

```sh
shasum -a 256 -c SHA256SUMS
```

The dylib source hash is `11cd73330db56c0c034fd642eb5cb88b4197f187199106b810b0469c450ab3bc`. The shipped binary was produced by [macOS workflow run 40234663019](https://github.com/benr00t3d/go-cgo-target-macos-poc/actions/runs/40234663019) from revision `5e7687349eed5fcf1221b742d9aae650b3c2291e` and then committed unchanged.

## References

- [Go Security Decisions](https://go.dev/doc/security/decisions)
- [Go Security Policy](https://go.dev/doc/security/policy)
- [VS Code Workspace Trust](https://code.visualstudio.com/docs/editing/workspaces/workspace-trust)
- [Clang configuration files](https://clang.llvm.org/docs/UsersManual.html#configuration-files)
- [GO-2022-0201 / CVE-2018-6574](https://pkg.go.dev/vuln/GO-2022-0201)
- [GO-2023-1841 / CVE-2023-29404](https://pkg.go.dev/vuln/GO-2023-1841)
