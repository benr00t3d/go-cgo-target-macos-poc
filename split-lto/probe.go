package main

// #cgo CFLAGS: -flto
// #cgo LDFLAGS: -l to_library ${SRCDIR}/candidate.dylib
import "C"

func main() {}
