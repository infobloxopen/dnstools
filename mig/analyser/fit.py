import sys
import json
import math

import numpy
from scipy.optimize import leastsq

def jload(name):
    with open(name) as f:
       data = json.load(f)

    sends = data.get("sends", [])
    receives = data.get("receives", [])
    pairs = data.get("pairs", [])

    receives = filter(lambda x: x > 0, receives)

    return sends, receives, pairs

__NANOSECONDS = 1e9

def calculate_fit(timestamps, start, params):
    def linear_count(x, timestamp):
        return x[0] + x[1]*(timestamp - start)/__NANOSECONDS

    def linear_count_residuals(x, timestamp, count):
        return linear_count(x, timestamp) - count

    if params:
        left, right = tuple(params[0])
    else:
        left = timestamps[0] - start
        right = timestamps[-1] - start

    points = [(timestamp, i + 1) for i, timestamp in enumerate(timestamps) if left <= timestamp - start <= right]
    timestamp_train, count_train = zip(*points)

    timestamp_train = numpy.array(timestamps)
    count_train = numpy.array(range(1, len(timestamps) + 1))

    x0 = params[1] if len(params) > 1 else [0., 1.]
    return leastsq(linear_count_residuals, numpy.array(x0),
                   args=(numpy.array(timestamp_train), numpy.array(count_train)))[0]

def main():
    path = sys.argv[1]

    sends, receives, pairs = jload(path)
    send = calculate_fit(sends, sends[0], [])[1]
    recv = calculate_fit(receives, sends[0], [])[1] if receives else 0
    lost = len(sends) - len(receives)

    print json.dumps({"send": send, "recv": recv, "lost": lost})
    return 0

if __name__ == '__main__':
    sys.exit(main())
