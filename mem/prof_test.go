package mem

import (
	"bytes"
	"io/ioutil"
	"os"
	"path"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestProf(t *testing.T) {
	w := bytes.NewBuffer(nil)
	if assert.NoError(t, Prof(w)) {
		assert.True(t, w.Len() > 0, "expected not empty memory profile")
	}
}

func TestProfToFile(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		assert.FailNow(t, "can't create temporary directory", "error: %s", err)
	}
	defer os.RemoveAll(dir)

	assert.NoError(t, ProfToFile(path.Join(dir, "test.pprof")))
	assert.NotEqual(t, nil, ProfToFile(""))
}
