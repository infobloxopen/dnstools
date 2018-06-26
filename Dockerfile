FROM alpine:latest as builder
LABEL builder=true
MAINTAINER John Belamaric <jbelamaric@infoblox.com> @johnbelamaric

RUN apk add --update --no-cache g++ make git bind bind-dev openssl-dev \
    libxml2-dev libcap-dev json-c-dev libcrypto1.0 file

RUN git clone https://github.com/akamai/dnsperf
RUN cd dnsperf && ./configure && make && strip dnsperf resperf

FROM alpine:latest
ENV PS1="dnstools# "
RUN apk --update --no-cache add bind-tools curl jq tcpdump libcrypto1.0

COPY --from=builder /dnsperf/dnsperf /bin
COPY --from=builder /dnsperf/resperf /bin

ENTRYPOINT ["/bin/sh"]
