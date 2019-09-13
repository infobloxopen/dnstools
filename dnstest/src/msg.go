package main

import (
	"bufio"
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

func getMsg(seed int64, uncached bool) *dns.Msg {
	i := seed % int64(len(domains))
	domain := domains[i]
	if uncached {
		domain = strconv.FormatInt(seed, 10) + "." + domain
	}
	msg := new(dns.Msg)
	msg.SetQuestion(domain, dns.TypeA)
	return msg
}
