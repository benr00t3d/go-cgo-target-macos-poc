# Email to Go Security

To: `security@golang.org`

Subject: `cmd/go: cgo -target validation bypass executes package code during analysis`

Hello Go Security Team,

I found a cgo flag-validation bypass in `cmd/go`. A package can supply a
path-bearing `-target` operand that makes Clang read a configuration file from
the package directory. That file can contain `-Xclang -load`, even though Go
rejects the same option when it appears directly in a `#cgo` directive.

The loaded dynamic library runs inside Clang while Go tooling requests compiled
package metadata. The target Go program is never built or run. Code in the
library executes with the permissions of the developer or analysis service.

The Apple-silicon macOS reproduction is:

```sh
DEMO_ROOT="$(mktemp -d /tmp/go-cgo-calculator.XXXXXX)"
gh repo clone benr00t3d/go-cgo-target-macos-poc "$DEMO_ROOT/repo"
code --new-window "$DEMO_ROOT/repo"
```

On the first analysis of a fresh clone, Calculator opens while the Go extension
indexes the workspace. This happens immediately in an already trusted location,
or after Workspace Trust is granted in a new location. I did not run a build,
test, task, or terminal command inside VS Code. The demonstration library only
opens Calculator and writes a local receipt.

The trigger is the compiler invocation during background analysis, not the
window-open event itself. A later reopen can reuse gopls state or Go's build
cache and may not invoke Clang or reload the library.

For repeated local demonstrations, close the VS Code window and reset the
visible application, language server, and Go build cache before reopening it:

```sh
/usr/bin/killall Calculator 2>/dev/null || true
pkill -x gopls 2>/dev/null || true
go clean -cache
```

This reset is only for repeating the visual demonstration. It is not required
for the first analysis of a fresh clone.

I reproduced the visual path on an M1 MacBook Air with macOS 15.4.1, Go 1.26.2,
Apple Clang 17.0.0, gopls v0.21.1, VS Code 1.143.1, and the Go extension v0.58.0.
I also reproduced the underlying `cmd/go` path on Linux with Go 1.26.10, Go
1.27.3, and Go tip using affected Clang versions.

Materials:

- Repository: https://github.com/benr00t3d/go-cgo-target-macos-poc
- Full report: https://github.com/benr00t3d/go-cgo-target-macos-poc/blob/main/disclosure/REPORT.md
- Clean reproduction revision: `0fd006c5f8af1268a2665bd0c51c0da64186643b`
- Earlier video-tested revision: `38ce6f0cbbd86bb9e33269215f78122c11036d86`
- Marker-only control tag: `macos-live-positive-20261025`
- Video: [private video URL]

The report includes the root cause, matched negative controls, artifact hashes,
and a suggested `-target` operand validation fix.

Preferred attribution: Robert Prast

Thank you,

Robert Prast
