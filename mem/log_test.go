package mem

import (
	"io/ioutil"
	"os"
	"path"
	"syscall"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestCreateLog(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		assert.FailNow(t, "can't create temporary directory", "error: %s", err)
	}
	defer os.RemoveAll(dir)

	l, err := createLog(path.Join(dir, "test.log.json"))
	if assert.NoError(t, err) {
		assert.NotEqual(t, (*log)(nil), l)
		assert.NoError(t, l.close())
	}

	l, err = createLog("")
	if err != nil {
		if pErr, ok := err.(*os.PathError); ok {
			assert.Equal(t, syscall.ENOENT, pErr.Err)
		} else {
			assert.Fail(t, "unknown error on log creation with empty path", "error: %s", err)
		}
	} else {
		defer l.close()
		assert.Fail(t, "expected error on log creation with empty path", "log: %#v", l)
	}
}

func TestLogWrite(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		assert.FailNow(t, "can't create temporary directory", "error: %s", err)
	}
	defer os.RemoveAll(dir)

	log := path.Join(dir, "test.log.json")

	l, err := createLog(log)
	assert.NoError(t, err)
	defer l.close()

	assert.NoError(t,
		l.write(struct {
			A bool
			B int
			C string
		}{
			A: true,
			B: 127,
			C: "test",
		}),
	)

	b, err := ioutil.ReadFile(log)
	assert.NoError(t, err)
	assert.Equal(t,
		"{\"A\":true,\"B\":127,\"C\":\"test\"}\n",
		string(b),
	)

	assert.NoError(t,
		l.write(struct {
			X bool
			Y int
			Z string
		}{
			Y: -128,
			Z: "example",
		}),
	)

	b, err = ioutil.ReadFile(log)
	assert.NoError(t, err)
	assert.Equal(t,
		"{\"A\":true,\"B\":127,\"C\":\"test\"}\n"+
			"{\"X\":false,\"Y\":-128,\"Z\":\"example\"}\n",
		string(b),
	)

	assert.NotEqual(t, nil, l.write(func() {}))
}
