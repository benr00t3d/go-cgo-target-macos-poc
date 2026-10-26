package splitlto

// #cgo CFLAGS: -flto
// #cgo LDFLAGS: -l to_library ${SRCDIR}/candidate.dylib
import "C"

// Present keeps the package non-empty without executing any Go code.
const Present = true
