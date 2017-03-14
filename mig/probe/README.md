# DNS Performance Measurement

# Tools

MIG consists of two tools DNS client and DNS server. Client can send A queries with specified domain names and register time of sending and receiving of each query. DNS server can be used to stub real DNS in case when performance of some forwarding stack is measured.

# Build

The utilities can be built on MAC OS and Linux with following:

```bash
make
```

There is "clean" target to remove build output.

Note:
- Some Linux distributives (like Fedora 12) may require "-lrt" flag in the very end of gcc command to be able to use "clock_gettime" syscall.

# Measuring Performance

In one terminal start stub DNS server:
```bash
./server -a 127.0.0.1 -p 5353
```

In the other terminal run mig against the DNS server:
```bash
./mig -s 127.0.0.1 -p 5353 -d domains.lst -n 10000 -o test.json
```

This sends 10000 A requests with domain names listed in domain.lst (plain text file with one domain per line) to 127.0.0.1 at 5353 port. Resulting timings go to test.json. The tool prints following to standard output:
```
[03/14/17 10:43:48] Starting...
[03/14/17 10:43:48] Messages:
	Sent....: 10000;
	Received: 10000;
	Lost....: 0.


[03/14/17 10:43:48] Exiting...
```

This mean that it sent 10000 queries received all replies and there is no any lost query. Stub server output is following:
```
[03/14/17 10:41:13] Starting...
[03/14/17 10:41:13] Got stdin flags 0x2.
[03/14/17 10:41:13] Made stdin nonblocking (0x6).
[03/14/17 10:41:13] Got socket 3.
[03/14/17 10:41:13] Got socket flags 0x2.
[03/14/17 10:41:13] Made socket nonblocking (0x6).
[03/14/17 10:41:13] Bound to 127.0.0.1:5353.
[03/14/17 10:41:13] Allocated 65535 bytes for receiver buffer.
[03/14/17 10:41:13] Allocated 65535 bytes for sender buffer.
[03/14/17 10:41:13] Created message queue of 104857600 bytes.
[03/14/17 10:41:13] Serving...
[03/14/17 10:43:49] Got 10000 message(s).
```
Which confirms that it got all the messages sent by the tool.

Result timings looks like simple JSON object:
```json
{"sends":
  [
    2166611145345000,
    2166611145374606,
    ...,
    2166611237627733
  ],
 "receives":
  [
    2166611145475266,
    2166611145477046,
    ...,
    2166611237677273
  ],
 "pairs":
  [
    [2166611145345000, 2166611145475266, 130266],
    [2166611145374606, 2166611145477046, 102440],
    ...
    [2166611237627733, 2166611237677273, 49540]
  ]
}
```

Here:
  - sends - sorted list of sent timestamps (timestamp at N position means that to the time the tool has sent N messages);
  - receives - sorted list of received timestamps (similary here timestamp at N position means that to the time the tool has received N messages). If some messages have been lost receives contains corresponding number of zeroes at the end;
  - pairs - sorted by send time timestamp of sending query and timestamp of receiving reply to that query (so second value can be not ordered if replies went in different order from server); Third number is difference of previous two. If respose for particular query hasn't arrived its list would contain only one number (timestamp when the query has been sent).

Example of domains.lst:
```
tushs.com
tames.com
scarp.com
skell.com
fudge.com
ikons.com
```
