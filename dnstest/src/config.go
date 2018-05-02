package main

import (
	"flag"
	"fmt"
)

type config struct {
	mode     string
	server   string
	caPath   string
	clients  int
	workers  int
	timeout  int
	uncached bool
}

func loadConfig() *config {
	cfg := new(config)

	flag.StringVar(&cfg.mode, "p", "udp", "Protocol [udp, tcp, tls]")
	flag.StringVar(&cfg.server, "s", "127.0.0.1:53", "Server endpoint (addr:port)")
	flag.StringVar(&cfg.caPath, "c", "publickey.cer", "CA cert path")
	flag.IntVar(&cfg.clients, "n", 100, "Number of clients to emulate")
	flag.IntVar(&cfg.workers, "w", 100, "Number of workers")
	flag.IntVar(&cfg.timeout, "l", 10, "Time limit (seconds)")
	flag.BoolVar(&cfg.uncached, "u", false, "Use uncached queries")

	flag.Parse()

	if cfg.mode != "udp" && cfg.mode != "tcp" && cfg.mode != "tls" {
		panic(fmt.Errorf("Protocol is wrong (%s)!", cfg.mode))
	}

	return cfg
}
