package mem

import (
	"math"
	"runtime"
	"sync"
	"time"
)

// A StatsLogger writes periodically runtime.MemStats to a file.
type StatsLogger struct {
	l *log
	c chan struct{}
	w *sync.WaitGroup
}

// StartStatsLogger creates new StatsLogger and starts it.
func StartStatsLogger(path string, d time.Duration) (*StatsLogger, error) {
	sl := &StatsLogger{
		c: make(chan struct{}),
		w: new(sync.WaitGroup),
	}

	l, err := createLog(path)
	if err != nil {
		return nil, err
	}
	sl.l = l

	sl.w.Add(1)
	go sl.run(d)

	return sl, nil
}

// Stop terminates running logger. It waits until the logger really stops.
func (sl *StatsLogger) Stop() error {
	close(sl.c)
	sl.w.Wait()

	return sl.l.close()
}

func (sl *StatsLogger) run(d time.Duration) {
	defer sl.w.Done()

	var m runtime.MemStats
	p := &m

	prev := time.Now().UnixNano()
	runtime.ReadMemStats(p)

	ngc := m.NumGC

	pts := newPoints()
	pts.set(prev, p)
	pts.write(sl.l)

	t := time.NewTicker(d)
	defer t.Stop()

	for {
		select {
		case <-t.C:
		case _, ok := <-sl.c:
			if !ok {
				return
			}
		}

		curr := time.Now().UnixNano()
		runtime.ReadMemStats(p)

		pts.set(curr, p)

		if ngc != m.NumGC || time.Duration(curr-prev) > 10*time.Second {
			pts.write(sl.l)

			ngc = m.NumGC
			prev = curr
		}
	}
}

type point struct {
	T int64
	M runtime.MemStats
}

type points struct {
	min point
	max point
}

func newPoints() *points {
	pts := new(points)
	pts.min.M.Alloc = math.MaxUint64
	pts.max.M.Alloc = math.MaxUint64

	return pts
}

func (pts *points) set(t int64, p *runtime.MemStats) {
	if pts.min.M.Alloc == math.MaxUint64 || pts.min.M.Alloc > p.Alloc {
		pts.min.T = t
		pts.min.M = *p
	}

	if pts.max.M.Alloc == math.MaxUint64 || pts.max.M.Alloc < p.Alloc {
		pts.max.T = t
		pts.max.M = *p
	}
}

func (pts *points) write(l *log) {
	if pts.min.T < pts.max.T {
		check(l.write(pts.min))
		check(l.write(pts.max))
	} else if pts.min.T > pts.max.T {
		check(l.write(pts.max))
		check(l.write(pts.min))
	} else {
		check(l.write(pts.min))
	}

	pts.min.M.Alloc = math.MaxUint64
	pts.max.M.Alloc = math.MaxUint64
}

func check(err error) {
	if err != nil {
		panic(err)
	}
}
