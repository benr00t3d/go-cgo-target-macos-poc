# Email draft

**To:** `security@golang.org`  
**Subject:** `Vulnerability: cmd/go: cgo -target validation bypass executes repository native code during analysis`

Hello Go Security Team,

I am reporting a `cmd/go` cgo flag-validation bypass that lets an attacker-controlled Go workspace or module execute a repository-supplied native library during compiled-metadata analysis, without running the target Go program.

A package can provide:

```go
/*
#cgo darwin,arm64 CFLAGS: -target arm64-cfg/poc-darwin
*/
import "C"
```

`cmd/go` accepts `-target` plus this path-bearing operand. Apple Clang derives `arm64-cfg/poc-darwin-clang.cfg` from it, reads that file relative to the cgo package directory, and the repository-controlled config restores the otherwise rejected option pair:

```text
-Xclang -load -Xclang <CFGDIR>/calculator_plugin.dylib
```

The dylib constructor therefore runs inside Clang when tooling requests compiled package metadata. A direct `#cgo CFLAGS: -Xclang -load ...` control is rejected by Go; the target-derived config bypasses that protection.

I reproduced this on an M1 MacBook Air with macOS 15.4.1, Go 1.26.2, Apple Clang 17.0.0, gopls v0.21.1, VS Code 1.143.1, and the Go extension v0.58.0. In an already trusted location, opening the repository folder caused the benign demonstration library to open Calculator automatically. In a fresh `/tmp` clone, VS Code correctly entered Restricted Mode; clicking Trust caused Calculator to open without a further click and without running Build, Run, Test, a task, or a terminal command. I also reproduced the core `go list -compiled` path with matched controls on macOS, and with Go 1.26.10, Go 1.27.3, and tip using Clang 16, 18, and 22 on Linux.

The impact is arbitrary native code execution with the analyzing user's permissions. The VS Code demonstration requires the user to open/trust the workspace, but the underlying trigger is Go compiled-metadata loading and is also reachable from gopls initialization and official govulncheck analysis. This seems to match Go's published security decision that a malicious module causing a toolchain command to execute arbitrary code is a vulnerability.

The full report is here:

`https://github.com/benr00t3d/go-cgo-target-macos-poc/blob/main/REPORT.md`

The minimal public reproduction repository is here:

`https://github.com/benr00t3d/go-cgo-target-macos-poc`

I have also recorded a video showing the trusted-location and Restricted-Mode/Trust flows:

`[ADD PRIVATE VIDEO LINK]`

The video-tested PoC revision is:

`38ce6f0cbbd86bb9e33269215f78122c11036d86`

The safe marker-only matched-control verifier is retained in the same repository at tag `macos-live-positive-20261025`. `REPORT.md` includes the root-cause analysis, hashes, exact observed results, version matrix, limitations, and suggested fix.

Preferred attribution: `[ADD NAME / HANDLE / URL, OR DELETE THIS LINE]`

Thank you,

Robert
