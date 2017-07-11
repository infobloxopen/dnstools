FROM alpine:latest
MAINTAINER John Belamaric <jbelamaric@infoblox.com> @johnbelamaric

ENV PS1="dnstools# "
RUN apk --update add bind-tools curl jq && rm -rf /var/cache/apk/*

ENTRYPOINT ["/bin/sh"]
