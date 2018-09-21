package mem

import (
	"io/ioutil"
	"os"
	"os/signal"
	"path"
	"syscall"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestProfOnSignal(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		assert.FailNow(t, "can't create temporary directory", "error: %s", err)
	}
	defer os.RemoveAll(dir)

	pid := os.Getpid()
	p, err := os.FindProcess(pid)
	if err != nil {
		assert.FailNow(t, "can't find test process by its pid", "pid: %d, error: %s", pid, err)
	}

	prof := path.Join(dir, "test.pprof")

	errs := make(chan error, 1)
	stopped := false
	c := StartProfOnSignal(
		prof,
		func(err error) {
			errs <- err
		},
		syscall.SIGHUP,
	)
	defer func(c chan os.Signal) {
		if !stopped {
			StopProfOnSignal(c)
		}
	}(c)

	if err = p.Signal(syscall.SIGHUP); err != nil {
		assert.FailNow(t, "can't send signal to test process", "pid: %d, error: %s", pid, err)
	}

	d := 100 * time.Millisecond
	timeout := time.After(d)
	select {
	case err = <-errs:
		assert.NoError(t, err)
		assert.FileExists(t, prof)

	case <-timeout:
		assert.Fail(t, "no profile after timeout", "timeout: %s, profile: %s", d, prof)
	}

	os.Remove(prof)
	StopProfOnSignal(c)
	stopped = true

	c = make(chan os.Signal, 1)
	signal.Notify(c, syscall.SIGHUP)
	defer signal.Stop(c)

	if err = p.Signal(syscall.SIGHUP); err != nil {
		assert.FailNow(t, "can't send signal to test process", "pid: %d, error: %s", pid, err)
	}

	time.Sleep(d)
	f, err := os.Open(prof)
	if err != nil {
		if pErr, ok := err.(*os.PathError); ok {
			assert.Equal(t, syscall.ENOENT, pErr.Err, "found unexpected profile %s", prof)
		} else {
			assert.Fail(t, "unknown error while trying to open profile", "profile: %s, error: %s", prof, err)
		}
	} else {
		defer f.Close()
		assert.Fail(t, "found unexpected profile %s", prof)
	}
}
