# Static review receipt

Date: 2026-10-25 UTC

This is not a macOS execution receipt.

Observed locally on Linux:

- `bash -n verify-macos.sh` passed.
- `lsp_initialize_only.py` compiled as Python source.
- `plugin/marker.c` and `probe.c` passed Clang 22 syntax checks.
- Invoking the verifier on Linux stopped at its Darwin guard with status 2.
- No external system was contacted and no Mach-O binary was produced.

Source-informed expectation:

- Apple/Swift LLVM's Xcode-16-era `stable/20240723` fork at commit `6db1f39b`
  derives the default filename as `<triple>-<mode>.cfg`.
- Its config resolver directly treats a parent-bearing derived name as a path.
- The same fork contains `<CFGDIR>` substitution.
- Therefore `-target arm64-cfg/poc-darwin` is expected to look for
  `arm64-cfg/poc-darwin-clang.cfg`; the x86_64 spelling follows identically.

A live owned-Mac run is still required to establish Apple Clang behavior,
Mach-O loading, code-signing/library-validation behavior, cmd/go reachability,
and (optionally) gopls initialization. The verifier's `summary.txt` is the
decision receipt for that run.

