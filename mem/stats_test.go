package mem

import (
	"bufio"
	"encoding/json"
	"io/ioutil"
	"os"
	"path"
	"runtime"
	"syscall"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestStatsLogger(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		assert.FailNow(t, "can't create temporary directory", "error: %s", err)
	}
	defer os.RemoveAll(dir)

	log := path.Join(dir, "test.log.json")

	sl, err := StartStatsLogger(log, 100*time.Microsecond)
	assert.NoError(t, err)
	defer sl.Stop()

	time.Sleep(10 * time.Millisecond)
	for i := 0; i < 3; i++ {
		runtime.GC()
		time.Sleep(10 * time.Millisecond)
	}

	f, err := os.Open(log)
	assert.NoError(t, err)
	defer f.Close()

	scn := bufio.NewScanner(f)
	count := 0
	for scn.Scan() {
		count++
		var v interface{}
		assert.NoError(t, json.Unmarshal(scn.Bytes(), &v), "line %d", count)
	}

	assert.NoError(t, scn.Err())
	assert.True(t, count >= 3)

	sl, err = StartStatsLogger("", 100*time.Microsecond)
	if err != nil {
		if pErr, ok := err.(*os.PathError); ok {
			assert.Equal(t, syscall.ENOENT, pErr.Err)
		} else {
			assert.Fail(t, "unknown error on logger start with empty path", "error: %s", err)
		}
	} else {
		sl.Stop()
		assert.Fail(t, "expected error on logger start with empty path", "logger: %#v", sl)
	}
}
