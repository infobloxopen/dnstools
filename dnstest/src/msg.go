package main

import (
	"strconv"

	"github.com/miekg/dns"
)

const domainsCount = 10

var domains = [domainsCount]string{
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

func getMsg(seed int64, uncached bool) *dns.Msg {
	i := seed % domainsCount
	domain := domains[i]
	if uncached {
		domain = strconv.FormatInt(seed, 10) + "." + domain
	}
	msg := new(dns.Msg)
	msg.SetQuestion(domain, dns.TypeA)
	return msg
}
