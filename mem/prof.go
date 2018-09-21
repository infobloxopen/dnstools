// Package mem provides tools to measure profile program memory over time.
package mem

import (
	"io"
	"os"
	"runtime"
	"runtime/pprof"
)

// Prof forces garbage collection and writes heap profile using given writer.
func Prof(w io.Writer) error {
	runtime.GC()
	return pprof.WriteHeapProfile(w)
}

// ProfToFile dumps memory profile to a file.
func ProfToFile(path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	return Prof(f)
}
