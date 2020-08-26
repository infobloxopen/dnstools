package main

import (
	"crypto/tls"
	"fmt"
	"sync/atomic"
	"time"

	"github.com/miekg/dns"
)

type dnstest struct {
	mode           string
	server         string
	start          time.Time
	tlsConfig      *tls.Config
	conns          chan *dns.Conn
	uncached       bool
	ecs            bool
	readTimeout    time.Duration
	dialTimeout    time.Duration
	maxDialTimeout time.Duration
	totalCount     int64
	totalRTT       int64
}

func newTest(cfg *config) *dnstest {
	ret := &dnstest{
		mode:           cfg.mode,
		server:         cfg.server,
		start:          time.Now(),
		conns:          make(chan *dns.Conn, cfg.clients),
		uncached:       cfg.uncached,
		readTimeout:    time.Duration(cfg.readTimeout) * time.Second,
		dialTimeout:    time.Duration(cfg.dialTimeout) * time.Second,
		maxDialTimeout: time.Duration(cfg.maxDialTimeout) * time.Second,
		ecs:            cfg.ecs,
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
	t := time.Now()
	if dt.tlsConfig != nil {
		conn, err = dns.DialTimeoutWithTLS("tcp", dt.server, dt.tlsConfig, dt.maxDialTimeout)
	} else {
		conn, err = dns.DialTimeout(dt.mode, dt.server, dt.maxDialTimeout)
	}
	since := time.Since(t)
	if since > dt.dialTimeout {
		fmt.Printf("Dial warning - delay: %s\n", since)
	}

	if err != nil {
		fmt.Printf("Dial error: %s\n", err)
		return nil
	}

	return conn
}

func (dt *dnstest) req() *dns.Msg {
	seed := atomic.AddInt64(&dt.totalCount, 1)
	return getMsg(seed, dt.uncached, dt.ecs)
}

func (dt *dnstest) exchange(conn *dns.Conn, req *dns.Msg) error {
	reqTime := time.Now()
	if err := conn.WriteMsg(req); err != nil {
		return err
	}
	conn.SetReadDeadline(time.Now().Add(dt.readTimeout))
	resp, err := conn.ReadMsg()
	if err != nil {
		return err
	}
	if resp.Id != req.Id {
		return dns.ErrId
	}
	if resp.Rcode != dns.RcodeSuccess && resp.Rcode != dns.RcodeNameError {
		return fmt.Errorf("bad rcode: %s", dns.RcodeToString[resp.Rcode])
	}
	rtt := int64(time.Since(reqTime))
	atomic.AddInt64(&dt.totalRTT, rtt)
	return nil
}

func (dt *dnstest) worker() {
	for conn := range dt.conns {
		if conn == nil {
			conn = dt.connect()
			if conn == nil {
				fmt.Printf("Reconnect due connection was broken\n")
				dt.conns <- nil
				continue
			}
		}
		req := dt.req()
		if err := dt.exchange(conn, req); err != nil {
			conn.Close()
			fmt.Printf("Reconnect for [%s] due %s\n", req.Question[0].Name, err)
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
