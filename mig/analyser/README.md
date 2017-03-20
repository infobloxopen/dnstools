# DNS Performance Analysis

# Tools

MIG comes with following three utilites for analysis: preview, fit and grinder. Preview utility is helper for organizing binary search. Fit fits send and receive sequences linearly and prints sending and receiving QpS. Grinder builds report on series of MiG runs.

## Preview
Usage:
```bash
python preview.py <test.json> <low> <high> <current>
```
Where:
- test.json - path to MiG output JSON;
- low - previous low limit;
- high - previous high limit;
- current - limit with which test.json has been made.

Preview fits sending trend in test.json. Then finds next limit based on following:
- (low + current)/2 if there are some queries with no answer;
- (high + current)/2 if all queries got answers.

And it prints JSON object to stdout with following keys:
- rate - rate rounded to 2 digits;
- lost - number queries with no answers;
- next - next limit for MiG to perform binary search (rounded as well to 2 digits).

## Fit
Usage:
```bash
python fit.py <test.json>
```
Where:
- test.json - path to MiG output JSON.

The utility fits send and receive trends form MiG output and prints following JSON to stdout:
- send - sending rate;
- recv - receiving rate;
- lost - number queries with no answers.

## Grinder
Usage:
```bash
python grinder.py -o <report.html> -t <report title> --rates <path>/<to>/<results>/
```

Where:
- <report.html> - report file name;
- &lt;report title&gt; - title of the report (placed in title tag of html);
- &lt;path&gt;/&lt;to&gt;/&lt;results&gt;/ - directory where stored set of MiG output files.

Grinder creates html report with interactive charts based on given results. 

For example run MiG several times with different limits:
```bash
./mig -s 127.0.0.1 -p 5353 -n 100000 -d domains.lst -o /tmp/test/test-10000.json -l 10000
./mig -s 127.0.0.1 -p 5353 -n 100000 -d domains.lst -o /tmp/test/test-11000.json -l 11000
./mig -s 127.0.0.1 -p 5353 -n 100000 -d domains.lst -o /tmp/test/test-12000.json -l 12000
...
./mig -s 127.0.0.1 -p 5353 -n 100000 -d domains.lst -o /tmp/test/test-20000.json -l 20000
```

It produces several files like `/tmp/test/test-10000.json`, `/tmp/test/test-11000.json`, ..., `/tmp/test/test-20000.json`. Then run grinder:
```bash
python grinder.py -o test.html -t 'My Test' --rates /tmp/test/
```

Grinder collects all files from `/tmp/test/` matching `test-\d+.json` regex and builds `test.html` report.
