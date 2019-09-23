package main

import (
	"bufio"
	"math/rand"
	"os"
	"strconv"

	"github.com/miekg/dns"
)

var domains = []string{
	"amazon.com.",
	"bing.com.",
	"ebay.com.",
	"facebook.com.",
	"google.com.",
	"instagram.com.",
	"linkedin.com.",
	"twitter.com.",
	"yahoo.com.",
	"youtube.com.",
}

func loadData(filepath string) error {
	if filepath == "" {
		return nil
	}
	handle, err := os.Open(filepath)
	if err != nil {
		return err
	}
	defer handle.Close()
	scanner := bufio.NewScanner(handle)

	domains = []string{}
	for scanner.Scan() {
		line := scanner.Text()
		if line[len(line)-1] != '.' {
			line += "."
		}
		domains = append(domains, line)
	}
	return nil
}

func randIP() []byte {
	o1 := byte(rand.Intn(254) + 1)
	o2 := byte(rand.Intn(254) + 1)
	o3 := byte(rand.Intn(254) + 1)
	return []byte{o1, o2, o3, 0}
}

func getMsg(seed int64, uncached, ecs bool) *dns.Msg {
	i := seed % int64(len(domains))
	domain := domains[i]
	if uncached {
		domain = strconv.FormatInt(seed, 10) + "." + domain
	}
	msg := new(dns.Msg)
	msg.SetQuestion(domain, dns.TypeA)
	if !ecs {
		return msg
	}
	msg.Extra = []dns.RR{
		&dns.OPT{
			Hdr: dns.RR_Header{Name: ".", Rrtype: dns.TypeOPT, Class: 4096},
			Option: []dns.EDNS0{
				&dns.EDNS0_SUBNET{
					Code:          dns.EDNS0SUBNET,
					SourceNetmask: 24,
					Family:        1, // ipv4
					Address:       randIP(),
				}},
		},
	}
	return msg
}
