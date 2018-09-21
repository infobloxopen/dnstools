package mem

import (
	"os"
	"os/signal"
)

// StartProfOnSignal puts ProfToFile as handler for given signal. If function onProf isn't nil it is called after each ProfToFile call with result of latest as an argument.
func StartProfOnSignal(path string, onProf func(err error), sig ...os.Signal) chan os.Signal {
	c := make(chan os.Signal, 1)
	signal.Notify(c, sig...)
	go handler(c, path, onProf)

	return c
}

// StopProfOnSignal stops memory profiling with given signal channel.
func StopProfOnSignal(c chan os.Signal) {
	signal.Stop(c)
	close(c)
}

func handler(c chan os.Signal, path string, onProf func(err error)) {
	for {
		if _, ok := <-c; !ok {
			break
		}

		if err := ProfToFile(path); onProf != nil {
			onProf(err)
		}
	}
}
