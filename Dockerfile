FROM alpine:latest
MAINTAINER John Belamaric <jbelamaric@infoblox.com> @johnbelamaric

RUN apk --update add bind-tools curl && rm -rf /var/cache/apk/*

ENTRYPOINT ["/bin/sh"]
