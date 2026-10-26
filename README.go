// Minimal macOS Calculator PoC
//
// From a terminal with GitHub CLI and the VS Code CLI installed:
//
//	DEMO_ROOT="$(mktemp -d /tmp/go-cgo-calculator.XXXXXX)"
//	gh repo clone benr00t3d/go-cgo-target-macos-poc "$DEMO_ROOT/repo"
//	code --new-window "$DEMO_ROOT/repo"
//
// On an Apple-silicon Mac, grant Workspace Trust when prompted and leave the
// folder untouched. VS Code opens this file, the Go extension starts gopls,
// and Calculator opens. No build, run, task, or terminal command is required
// after the folder is opened.
package calculator_demo

/*
#cgo darwin,arm64 CFLAGS: -target arm64-cfg/poc-darwin
*/
import "C"
