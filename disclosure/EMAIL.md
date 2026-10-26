# Email draft

To: `security@golang.org`

Subject: `Vulnerability: cmd/go: cgo -target validation bypass executes repository code during analysis`

Hello Go Security Team,

I found a cgo flag-validation bypass in `cmd/go`. A package can use a path-bearing `-target` operand to make Clang read a configuration file from the package directory. That configuration can contain `-Xclang -load`, even though Go rejects the same option when it is supplied directly in a `#cgo` directive.

The loaded dylib runs inside Clang while tools request compiled package metadata. The target Go program is not run.

I reproduced this on an M1 MacBook Air with macOS 15.4.1, Go 1.26.2, Apple Clang 17.0.0, and gopls v0.21.1. The visual PoC opens Calculator. In an already trusted VS Code location, it fired when the folder opened. VS Code trust is inherited by subfolders, so the same applies to repositories cloned below a trusted source, Downloads, or temporary directory. In a fresh `/tmp` clone, the PoC fired as soon as I granted Workspace Trust. I did not run a build, test, task, or terminal command inside VS Code.

The report, reproduction, source, and hashes are here:

https://github.com/benr00t3d/go-cgo-target-macos-poc/blob/main/disclosure/REPORT.md

The video-tested revision is `38ce6f0cbbd86bb9e33269215f78122c11036d86`. A marker-only verifier with matched controls is retained at tag `macos-live-positive-20261025`.

Video: [private video URL]

Preferred attribution: [name or handle]

Thank you,

Robert
