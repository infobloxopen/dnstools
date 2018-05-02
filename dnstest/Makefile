SRCROOT := $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))
SRC := github.com/infobloxopen/dnstools/dnstest/src
DNSREPO := github.com/miekg/dns

default: build

bootstrap:
	git clone --depth=1 https://$(DNSREPO) $(SRCROOT)/vendor/$(DNSREPO)

build:
	CGO_ENABLED=0 go build -i -v -ldflags '-w -s' -o $(SRCROOT)/build/dnstest $(SRC)

.PHONY: bootstrap build
