SRCROOT := $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))
SRC := github.com/infobloxopen/dnstools/dnstest/src

default: build

.PHONY: vendor
vendor:
	GO111MODULE=on go mod vendor

build:
	GO111MODULE=off CGO_ENABLED=0 go build -i -v -ldflags '-w -s' -o $(SRCROOT)/build/dnstest $(SRC)

.PHONY: vendor build
