// Open a fresh clone in VS Code on an Apple-silicon Mac with the Go extension.
// After Workspace Trust is granted, gopls loads this package and Calculator opens.
// No build, run, task, or terminal command is required.
package calculator_demo

/*
#cgo darwin,arm64 CFLAGS: -target arm64-cfg/poc-darwin
*/
import "C"
