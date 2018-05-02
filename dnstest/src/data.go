package main

import (
	"crypto/tls"
	"fmt"
	"sync/atomic"
	"time"

	"github.com/miekg/dns"
)

type dnstest struct {
	mode       string
	server     string
	start      time.Time
	tlsConfig  *tls.Config
	conns      chan *dns.Conn
	uncached   bool
	totalCount int64
	totalRTT   int64
}

func newTest(cfg *config) *dnstest {
	ret := &dnstest{
		mode:     cfg.mode,
		server:   cfg.server,
		start:    time.Now(),
		conns:    make(chan *dns.Conn, cfg.clients),
		uncached: cfg.uncached,
	}

	if cfg.mode == "tls" {
		tlsConfig, err := newTLSClientConfig(cfg.caPath)
		if err != nil {
			panic(fmt.Errorf("Init TLS config failed: %s", err))
		}
		ret.tlsConfig = tlsConfig
	}

	for i := 0; i < cfg.clients; i++ {
		ret.conns <- nil
	}

	return ret
}

func (dt *dnstest) connect() *dns.Conn {
	var (
		conn *dns.Conn
		err  error
	)
	if dt.tlsConfig != nil {
		conn, err = dns.DialWithTLS("tcp", dt.server, dt.tlsConfig)
	} else {
		conn, err = dns.Dial(dt.mode, dt.server)
	}
	if err != nil {
		panic(fmt.Errorf("Connect failed: %s", err))
	}
	return conn
}

func (dt *dnstest) exchange(conn *dns.Conn, hdr *dns.Header) error {
	seed := atomic.AddInt64(&dt.totalCount, 1)
	msg := getMsg(seed, dt.uncached)
	reqTime := time.Now()
	if err := conn.WriteMsg(msg); err != nil {
		return err
	}
	if _, err := conn.ReadMsgHeader(hdr); err != nil {
		return err
	}
	if hdr.Id != msg.Id {
		return dns.ErrId
	}
	if rcode := hdr.Bits & 15; rcode != dns.RcodeSuccess && rcode != dns.RcodeNameError {
		return fmt.Errorf("bad rcode: %d", rcode)
	}
	rtt := int64(time.Since(reqTime))
	atomic.AddInt64(&dt.totalRTT, rtt)
	return nil
}

func (dt *dnstest) worker() {
	hdr := new(dns.Header)
	for conn := range dt.conns {
		if conn == nil {
			conn = dt.connect()
		}
		if err := dt.exchange(conn, hdr); err != nil {
			conn.Close()
			fmt.Printf("Reconnect due %s\n", err)
			conn = nil
		}
		dt.conns <- conn
	}
}

const nsInSec = 1000000000

func (dt *dnstest) stats() (int64, float64) {
	count := atomic.LoadInt64(&dt.totalCount)
	if count == 0 {
		return 0, 0
	}
	rttNs := atomic.LoadInt64(&dt.totalRTT)
	qps := count / int64(time.Since(dt.start)/nsInSec)
	rtt := float64(rttNs/count) / nsInSec
	return qps, rtt
}
