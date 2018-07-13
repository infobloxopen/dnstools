package main

import (
	"fmt"
	"time"
)

func main() {
	cfg := loadConfig()

	test := newTest(cfg)

	for w := 0; w < cfg.workers; w++ {
		go test.worker()
	}

	<-time.After(time.Duration(cfg.limit) * time.Second)

	qps, rtt := test.stats()
	fmt.Printf("QPS: %d, RTT (sec): %f\n", qps, rtt)
}
