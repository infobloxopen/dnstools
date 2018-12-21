package mem

import (
	"bufio"
	"encoding/json"
	"os"
)

type log struct {
	f *os.File
	w *bufio.Writer
	e *json.Encoder
}

func createLog(path string) (*log, error) {
	f, err := os.Create(path)
	if err != nil {
		return nil, err
	}

	w := bufio.NewWriter(f)

	return &log{
		f: f,
		w: w,
		e: json.NewEncoder(w),
	}, nil
}

func (l *log) close() error {
	if err := l.w.Flush(); err != nil {
		l.f.Close()
		return err
	}

	return l.f.Close()
}

func (l *log) write(v interface{}) error {
	if err := l.e.Encode(v); err != nil {
		return err
	}

	if err := l.w.Flush(); err != nil {
		return err
	}

	return l.f.Sync()
}
