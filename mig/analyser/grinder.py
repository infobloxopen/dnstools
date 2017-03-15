# -*- coding: utf-8 -*-
import argparse
import sys
import json
import math
import os
import re

import numpy
from scipy.optimize import leastsq

def get_arguments():
    parser = argparse.ArgumentParser(description="Grinder - utility to visualize and process MIG output")
    parser.add_argument("-o", "--output",
                        help="file to write output to (default stdout)")
    parser.add_argument("-t", "--title",
                        default="Unknown",
                        help='title of HTML file (default "Unknown")')
    parser.add_argument("-d", "--details", action="append",
                        default=[],
                        help="additional marker for data")
    parser.add_argument("--from-point", type=int, default=None,
                        help="left data limit (ignore all data before the point)")
    parser.add_argument("--to-point", type=int, default=None,
                        help="right data limit (ignore all data after the point)")
    parser.add_argument("--no-fit", action="store_true", default=False,
                        help="ignore fits section in input files")
    parser.add_argument("--rates", action="store_true", default=False,
                        help="build recive rate vs sending rate chart")
    parser.add_argument("inputs", nargs='*',
                        help="files or directory with MIG data (if file name matches pattern test-<number>.json and "
                             "deatils aren't provided grinder uses <number>/1000k as label", metavar="INPUT")

    arguments = parser.parse_args()
    return arguments.output, arguments.title, arguments.details, arguments.inputs, \
           arguments.from_point, arguments.to_point, arguments.no_fit, arguments.rates

__SENDS_MARK = "sends"
__RECEIVES_MARK = "receives"
__PAIRS_MARK = "pairs"
__FIT_MARK = "fits"

def load(name):
    sends = []
    def enumerate_sends(line):
        try:
            timestamp = int(line)
        except ValueError:
            return find_section

        if sends and timestamp < sends[-1]:
            raise Exception("Message %d has been sent earlier than message %d (%d < %d)" %
                            (len(sends) + 1, len(sends), timestamp, sends[-1]))

        sends.append(timestamp)
        return enumerate_sends

    receives = []
    def enumerate_receives(line):
        try:
            timestamp = int(line)
        except ValueError:
            return find_section

        if timestamp == 0:
            return find_section

        if receives and timestamp < receives[-1]:
            raise Exception("Message %d has been received earlier than message %d (%d < %d)" %
                            len(receives) + 1, len(receives), timestamp, receives[-1])

        receives.append(timestamp)
        return enumerate_receives

    pairs = []
    def enumerate_pairs(line):
        if "," not in line:
            return find_section

        processing = line.split(",")
        if len(processing) < 3:
            return enumerate_pairs

        pairs.append(int(processing[2].strip()))
        return enumerate_pairs

    fits = {__SENDS_MARK: [],
            __RECEIVES_MARK: []}

    def enumerate_fits(line):
        if ":" not in line:
            return find_section

        name, rest = line.split(":", 1)
        fit = fits.get(name)
        if fit is None:
            return find_section

        fit.append(json.loads("[%s]" % rest.strip()))
        return enumerate_fits

    sections = {__SENDS_MARK: enumerate_sends,
                __RECEIVES_MARK: enumerate_receives,
                __PAIRS_MARK: enumerate_pairs,
                __FIT_MARK: enumerate_fits}

    def find_section(line):
        return sections.get(line.split(":", 1)[0], find_section)

    processor = find_section
    with open(name) as f:
        for line in f.readlines():
            processor = processor(line.strip().lower())

    return sends, receives, pairs, fits

def jload(name):
    with open(name) as f:
       data = json.load(f)

    sends = data.get("sends", [])
    receives = data.get("receives", [])
    pairs = data.get("pairs", [])
    fits = data.get("fits", {})

    receives = filter(lambda x: x > 0, receives)

    if __SENDS_MARK not in fits:
        fits[__SENDS_MARK] = []

    if __RECEIVES_MARK not in fits:
        fits[__RECEIVES_MARK] = []

    return sends, receives, pairs, fits

NAME_REGEX = re.compile("test\\-(\\d+)\\.json$", flags=re.IGNORECASE)

def enumerate_inputs(inputs):
    count = 0
    for item in inputs:
        if os.path.isdir(item):
            for name in os.listdir(item):
                if os.path.splitext(name)[1].lower() == ".json":
                    match = NAME_REGEX.match(name)
                    if match:
                        speed = int(match.group(1))
                    else:
                        speed = None

                    yield count, os.path.join(item, name), speed
                    count += 1
        else:
            yield count, item, None
            count += 1

__POINTS = 25
__FITPOINTS = 500
__MICROSECOND = 1e3
__MILLISECOND = 1e6
__NANOSECONDS = 1e9

def make_count(timestamps, name, start, fit, color):
    data = []
    step = len(timestamps)/(__POINTS if fit else __FITPOINTS)

    if step <= 1:
        for i, timestamp in enumerate(timestamps):
            data.append(((timestamp - start)/__MILLISECOND, i + 1))
    else:
        data.append(((timestamps[0] - start)/__MILLISECOND, 1))
        for i, timestamp in enumerate(timestamps[step - 1::step]):
            data.append(((timestamp - start)/__MILLISECOND, (i + 1)*step))

        if len(timestamps) > (i + 1)*step:
            data.append(((timestamps[-1] - start)/__MILLISECOND, len(timestamps)))

    return {"type": "scatter" if fit else "line", "data": data, "name": name, "color": color}

def make_rate(timestamps, name, start, fit, color):
    data = []
    step = len(timestamps)/(__POINTS if fit else __FITPOINTS)

    if step <= 1:
        previous = timestamps[0]
        count = 1
        for i, timestamp in enumerate(timestamps[1:]):
            if timestamp <= previous:
                count += 1
            else:
                data.append(((timestamp + previous - 2*start)/__MILLISECOND/2,
                              __NANOSECONDS*count/(timestamp - previous)))
                previous = timestamp
                count = 1
    else:
        previous = timestamps[0]
        steps = 1
        for i, timestamp in enumerate(timestamps[step - 1::step]):
            if timestamp <= previous:
                steps += 1
            else:
                data.append(((timestamp + previous - 2*start)/__MILLISECOND/2,
                              __NANOSECONDS*step*steps/(timestamp - previous)))
                previous = timestamp
                steps = 1

        if len(timestamps) > (i + 1)*step and timestamps[-1] > previous:
            data.append(((timestamps[-1] + previous - 2*start)/__MILLISECOND/2,
                         __NANOSECONDS*(len(timestamps) - (i + 1)*step)/(timestamps[-1] - previous)))

    return {"type": "scatter" if fit else "line", "data": data, "name": name, "color": color}

def make_total_processing(pairs, name, color):
    data = []
    step = len(pairs)/__FITPOINTS

    delays = sorted([pair[2] for pair in pairs if len(pair) > 2])
    if step <= 1:
        for i, delay in enumerate(delays):
            data.append((delay/__MILLISECOND, i + 1))
    else:
        data.append((delays[0]/__MILLISECOND, 1))
        for i, delay in enumerate(delays[step - 1::step]):
            data.append((delay/__MILLISECOND, (i + 1)*step))

        if len(delays) > (i + 1)*step:
            data.append((delays[-1]/__MILLISECOND, len(delays)))

    return {"data": data, "name": name, "color": color}

def make_processing(pairs, name, color):
    data = []
    data_steps = []

    delays = sorted([pair[2] for pair in pairs if len(pair) > 2])
    if delays:
        left = math.floor(math.log10(delays[0]))
        right = math.ceil(math.log10(delays[-1]))
        step = (right - left)/__FITPOINTS

        buckets = []
        for i in range(1, __FITPOINTS + 1):
            buckets.append([math.pow(10, left + i*step), []])

        index = 0
        bucket = buckets[index]
        for delay in delays:
            while delay > bucket[0] and index + 1 < len(buckets):
                index += 1
                bucket = buckets[index]

            bucket[1].append(delay)

        start = 0
        while start < len(buckets) and not buckets[start][1]:
            start += 1

        end = len(buckets)
        while end - 1 >= 0 and not buckets[end - 1][1]:
            end -= 1

        for i, bucket in enumerate(buckets[start:end]):
            count = len(bucket[1])
            if count > 1:
                x = float(sum(bucket[1]))/count/__MILLISECOND
                data.append((x, count))
                data_steps.append((x, (max(bucket[1]) - min(bucket[1]))/__MICROSECOND))
            else:
                data.append((bucket[0]/__MILLISECOND, 0))

    return {"data": data, "name": name, "color": color}, {"data": data_steps, "name": name, "color": color}

def make_queue(pairs, name, start, color):
    data = []
    step = len(pairs)/__FITPOINTS

    counter = []
    for pair in pairs:
        if len(pair) > 0:
            counter.append((pair[0], 1))

            if len(pair) > 1:
                counter.append((pair[1], -1))

    counter = sorted(counter, key=lambda x: x[0])

    queue = 0
    if step <= 1:
        for timestamp, increment in counter:
            queue += increment
            data.append(((timestamp - start)/__MILLISECOND, queue))
    else:
        timestamps = 0
        queues = 0
        for i, item in enumerate(counter):
            timestamp, increment = item
            queue += increment

            timestamps += timestamp
            queues += queue

            if (i + 1) % step == 0:
                data.append(((float(timestamps)/step - start)/__MILLISECOND, float(queues)/step))

                timestamps = 0
                queues = 0

        remainder = (i + 1) % step
        if remainder != 0:
            data.append(((float(timestamps)/remainder - start)/__MILLISECOND, float(queues)/remainder))

    return {"data": data, "name": name, "color": color}

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


def make_fit(timestamps, count_name, rate_name, start, params, color):
    fit = calculate_fit(timestamps, start, params)

    count_data = []
    rate_data = []
    time_step = (right - left)/__MILLISECOND/__FITPOINTS
    for i in range(__FITPOINTS):
        timestamp = time_step*i
        count_data.append((timestamp, linear_count(fit, timestamp*__MILLISECOND + start)))
        rate_data.append((timestamp, fit[1]))

    print "%s: %f QpS, delay: %f ms" % (rate_name, fit[1], -fit[0]/fit[1]*1e3)

    return {"data": count_data, "name": count_name, "marker": {"enabled": False}, "color": color}, \
           {"data": rate_data, "name": rate_name, "marker": {"enabled": False}, "color": color}

__COLORS = ('#7cb5ec', '#434348', '#90ed7d', '#f7a35c', '#8085e9', '#f15c80', '#e4d354', '#8085e8',
            '#8d4653', '#91e8e1')

def main():
    output, title, details, inputs, from_point, to_point, no_fit, build_rates = get_arguments()

    inputs = list(enumerate_inputs(inputs))
    for i, name, speed in inputs:
        if speed is None:
            def speed_translation(item):
                i, name, speed = item
                return i, name, None

    else:
        inputs = sorted(inputs, key=lambda x: x[2])

        def speed_translation(item):
            i, name, speed = item
            if speed % 1000 == 0:
                speed = "%dk" % (speed/1000)
            elif speed % 100 == 0:
                speed = "%.1fk" % (speed/1000.)
            elif speed % 10 == 0:
                speed = "%.2fk" % (speed/1000.)
            else:
                speed = "%.3fk" % (speed/1000.)

            return i, name, speed

    inputs = map(speed_translation, inputs)

    counts = []
    rates = []
    processing = []
    processing_steps = []
    queue = []
    reference_series = {"data": [], "name": "Reference"}
    receiving_series = {"data": [], "name": title}

    for i, name, speed in inputs:
        sends, receives, pairs, fits = jload(name)
        if no_fit:
            fits = {__SENDS_MARK: [], __RECEIVES_MARK: []}

        if from_point is not None and to_point is not None:
            sends = sends[from_point:to_point]
            receives = receives[from_point:to_point]
            pairs = pairs[from_point:to_point]

        elif from_point is not None:
            sends = sends[from_point:]
            receives = receives[from_point:]
            pairs = pairs[from_point:]

        elif to_point is not None:
            sends = sends[:to_point]
            receives = receives[:to_point]
            pairs = pairs[:to_point]

        start = sends[0] if sends else None

        send_color = __COLORS[2*i % len(__COLORS)]
        receive_color = __COLORS[(2*i + 1) % len(__COLORS)]

        additional = (details[i] if i < len(details) else "") or speed
        if additional:
            additional = " (%s)" % additional

        fits_sent_count = []
        fits_sending_rate = []
        fits_received_count = []
        fits_receiving_rate = []

        sending_rate = None
        for fit in fits[__SENDS_MARK]:
            fit_sent_count, fit_sending_rate = make_fit(sends, "Sent Fit%s" % additional,
                                                               "Sending Fit%s" % additional,
                                                        start, fit, send_color)
            if not fit and sending_rate is None and build_rates and fit_sending_rate["data"]:
                sending_rate = fit_sending_rate["data"][0][1]

            fits_sent_count.append(fit_sent_count)
            fits_sending_rate.append(fit_sending_rate)

        if sending_rate is None and build_rates:
            sending_rate = calculate_fit(sends, start, [])[1]
            print "%s: %s" % ((details[i] if i < len(details) else "") or speed, sending_rate)

        receiving_rate = None
        for fit in fits[__RECEIVES_MARK]:
            fit_received_count, fit_receiving_rate = make_fit(receives, "Received Fit%s" % additional,
                                                                        "Receiving Fit %s" % additional,
                                                              start, fit, receive_color)

            if not fit and receiving_rate is None and build_rates and fit_receiving_rate["data"]:
                receiving_rate = fit_receiving_rate["data"][0][1]

            fits_received_count.append(fit_received_count)
            fits_receiving_rate.append(fit_receiving_rate)

        if receiving_rate is None and build_rates:
            receiving_rate = calculate_fit(receives, start, [])[1]

        if build_rates and sending_rate is not None:
            reference_series["data"].append((sending_rate, sending_rate))

            if receiving_rate is not None:
                receiving_series["data"].append((sending_rate, receiving_rate))
                print "%s: %s" % ((details[i] if i < len(details) else "") or speed, receiving_rate)

        counts.append(make_count(sends, "Sent%s" % additional, start, fits_sent_count, send_color))
        counts += fits_sent_count

        rates.append(make_rate(sends, "Sending%s" % additional, start, fits_sending_rate, send_color))
        rates += fits_sending_rate

        counts.append(make_count(receives, "Received%s" % additional, start, fits_received_count, receive_color))
        counts += fits_received_count

        rates.append(make_rate(receives, "Receiving%s" % additional, start, fits_receiving_rate, receive_color))
        rates += fits_receiving_rate

        #processing.append(make_total_processing(pairs, "Delay%s" % additional, __COLORS[i % len(__COLORS)]))
        series, series_steps = make_processing(pairs, "Delay%s" % additional, __COLORS[i % len(__COLORS)])
        if i > 0:
            series["visible"] = False
            series_steps["visible"] = False

        processing.append(series)
        processing_steps.append(series_steps)

        queue.append(make_queue(pairs, "Queue%s" % additional, start, __COLORS[i % len(__COLORS)]))

    if build_rates:
        reference_series["data"] = sorted(reference_series["data"], key=lambda x: x[0])
        receiving_series["data"] = sorted(receiving_series["data"], key=lambda x: x[0])
        rate_chart = __HTML_RATE_CHART % (json.dumps(reference_series), json.dumps(receiving_series))
    else:
        rate_chart = ""

    content = __HTML % {"rate_chart": rate_chart,
                        "counts": json.dumps(counts),
                        "rates": json.dumps(rates),
                        "processing": json.dumps(processing),
                        "queue": json.dumps(queue),
                        "processing_steps": json.dumps(processing_steps),
                        "title": title,
                        "rate_div": __HTML_RATE_DIV if build_rates else ""}

    if output is None:
        sys.stdout.write(content)
    else:
        with open(output, 'w') as f:
            f.write(content)

    return 0

__HTML = r'''<!DOCTYPE HTML>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>DNS Queries - %(title)s</title>
        <script type="text/javascript" src="http://code.jquery.com/jquery-1.8.2.min.js"></script>
        <script type="text/javascript">
$(function () {
    %(rate_chart)s$('#counts').highcharts({
        colors: ['#4572A7', '#AA4643', '#89A54E', '#80699B', '#3D96AE', '#DB843D', '#92A8CD', '#A47D7C', '#B5CA92'],
        chart: {
            zoomType: 'xy'
        },
        title: {
            text: 'Messages'
        },
        xAxis: {
            title: {
                text: 'time, ms'
            }
        },
        yAxis: {
            title: {
                text: 'count'
            },
            plotLines: [{
                value: 0,
                width: 1,
                color: '#808080'
            }]
        },
        legend: {
            align: "right",
            verticalAlign: "middle",
            layout: "vertical"
        },
        credits: {enabled: false},
        series: %(counts)s
    });

    $('#rates').highcharts({
        chart: {
            zoomType: 'xy'
        },
        title: {
            text: 'Rates'
        },
        xAxis: {
            title: {
                text: 'time, ms'
            }
        },
        yAxis: {
            title: {
                text: 'rate, QpS'
            },
            plotLines: [{
                value: 0,
                width: 1,
                color: '#808080'
            }]
        },
        legend: {
            align: "right",
            verticalAlign: "middle",
            layout: "vertical"
        },
        credits: {enabled: false},
        series: %(rates)s
    });

    $('#processing').highcharts({
        chart: {
            zoomType: 'xy'
        },
        title: {
            text: 'Processing'
        },
        xAxis: {
            type: 'logarithmic',
            minorTickInterval: 0.1,
            title: {
                text: 'delay, ms'
            }
        },
        yAxis: {
            title: {
                text: 'count'
            },
            plotLines: [{
                value: 0,
                width: 1,
                color: '#808080'
            }]
        },
        legend: {
            align: "right",
            verticalAlign: "middle",
            layout: "vertical"
        },
        credits: {enabled: false},
        series: %(processing)s
    });

    $('#queue').highcharts({
        chart: {
            zoomType: 'xy'
        },
        title: {
            text: 'Processing'
        },
        xAxis: {
            title: {
                text: 'time, ms'
            }
        },
        yAxis: {
            title: {
                text: 'count'
            },
            plotLines: [{
                value: 0,
                width: 1,
                color: '#808080'
            }]
        },
        legend: {
            align: "right",
            verticalAlign: "middle",
            layout: "vertical"
        },
        credits: {enabled: false},
        series: %(queue)s
    });

    $('#processing_steps').highcharts({
        chart: {
            zoomType: 'xy'
        },
        title: {
            text: 'Processing'
        },
        xAxis: {
            type: 'logarithmic',
            minorTickInterval: 0.1,
            title: {
                text: 'delay, ms'
            }
        },
        yAxis: {
            type: 'logarithmic',
            minorTickInterval: 0.1,
            title: {
                text: 'interval, us'
            },
            plotLines: [{
                value: 0,
                width: 1,
                color: '#808080'
            }]
        },
        legend: {
            align: "right",
            verticalAlign: "middle",
            layout: "vertical"
        },
        credits: {enabled: false},
        series: %(processing_steps)s
    });
});
        </script>
    </head>
    <body>
        <script type="text/javascript" src="https://code.highcharts.com/highcharts.js"></script>
        %(rate_div)s<div id="counts" style="min-width: 310px; height: 700px; margin: 0 auto"></div>
        <div id="rates" style="min-width: 310px; height: 700px; margin: 0 auto"></div>
        <div id="processing" style="min-width: 310px; height: 700px; margin: 0 auto"></div>
        <div id="queue" style="min-width: 310px; height: 700px; margin: 0 auto"></div>
        <div id="processing_steps" style="min-width: 310px; height: 700px; margin: 0 auto"></div>
    </body>
</html>
'''

__HTML_RATE_DIV = r'''<div id="rate" style="min-width: 310px; height: 700px; margin: 0 auto"></div>
        '''

__HTML_RATE_CHART = r'''$('#rate').highcharts({
        colors: ['#4572A7', '#AA4643', '#89A54E', '#80699B', '#3D96AE', '#DB843D', '#92A8CD', '#A47D7C', '#B5CA92'],
        chart: {
            zoomType: 'xy'
        },
        title: {
            text: 'Receiving vs Sending'
        },
        xAxis: {
            title: {
                text: 'Sending, QpS'
            }
        },
        yAxis: {
            title: {
                text: 'Receiving, QpS'
            },
            plotLines: [{
                value: 0,
                width: 1,
                color: '#808080'
            }]
        },
        legend: {
            align: "right",
            verticalAlign: "middle",
            layout: "vertical"
        },
        credits: {enabled: false},
        series: [%s, %s]
    });

        '''

if __name__ == "__main__":
    sys.exit(main())
