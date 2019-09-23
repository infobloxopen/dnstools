# dnstest

This repository contains dnstest tool for testing DNS servers.

### Build

Install golang version >= 1.11
```
make vendor
make build
```

### Arguments

-c string
> CA cert path (for TLS mode - default "publickey.cer")

-e bool
> Insert random ECS to queries

-f string
> Data filename (use predefined list if empty), file content format - list of domains, one domain per line, predefined list (10 domains):
```
amazon.com
bing.com
ebay.com
facebook.com
google.com
instagram.com
linkedin.com
twitter.com
yahoo.com
youtube.com
```
-l int
> Time limit (seconds - default 10)

-n int
> Number of clients to emulate (default 100)

-p string
> Protocol [udp, tcp, tls] (default "udp")

-s string
> Server endpoint (addr:port - default "127.0.0.1:53")

-t int
> Read timeout (seconds - default 5)

-u bool
> Use uncached queries, random prefix (like 12345.google.com) is added for each domain in the list to avoid caching
