#!/usr/bin/env python3
"""Plot generation for the xv6 scheduler report (CS3.301 Mini Project 1).

Produces two figures:

  mlfq_timeline.png    section 2.3.2 -- queue occupancy over time, one colour
                       per process, with the 48-tick priority boosts marked.
  scheduler_comparison.png
                       section 2.2 -- average turnaround, waiting and
                       response time for FIFO, RR and MLFQ.

The timeline is built from the kernel's own trace output. Build with

    make qemu SCHEDULER=MLFQ TRACE=1 CPUS=1

and the kernel prints one line per tick per running or runnable process
(sleeping processes are not sampled, so they show up as gaps):

    MLFQTRACE <tick> <pid> <queue> <running?>

Capture the console output to a file and pass it in:

    python3 plot_mlfq.py trace.txt

Usage:
    python3 plot_mlfq.py [trace-file] [-o output-dir]
"""

import argparse
import os
import sys
from collections import defaultdict

import matplotlib

# Render to a file rather than a window -- this is run headless under WSL.
matplotlib.use("Agg")

import matplotlib.pyplot as plt

# The part before @ in chandrani.saha@research.iiit.ac.in, as required.
USERNAME = "chandrani.saha"

BOOST_INTERVAL = 48  # kernel/param.h
NQUEUE = 4           # kernel/param.h

# Measured with `schedulertest 4 5` on a single cpu, the same workload run
# under each scheduler in turn. Ticks.
COMPARISON = {
    "FIFO": {"turnaround": 265.50, "waiting": 148.00, "response": 2.00},
    "RR":   {"turnaround": 274.00, "waiting": 152.00, "response": 0.75},
    "MLFQ": {"turnaround": 197.00, "waiting": 75.25, "response": 0.75},
}


def watermark():
    """Stamp the author's username on the current axes, per the TAs' note."""
    plt.text(
        0.95, 0.95, USERNAME,
        ha='right', va='top',
        transform=plt.gca().transAxes,
        fontsize=10, color="gray", alpha=0.7
    )


def read_trace(path):
    """Parse MLFQTRACE lines into {pid: ([ticks], [queues])}.

    Anything that is not a trace line is ignored, so the raw console capture
    can be passed in without cleaning it up first.
    """
    series = defaultdict(lambda: ([], []))
    seen = 0

    with open(path, errors="replace") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) < 4 or parts[0] != "MLFQTRACE":
                continue
            try:
                tick, pid, queue = int(parts[1]), int(parts[2]), int(parts[3])
            except ValueError:
                continue
            series[pid][0].append(tick)
            series[pid][1].append(queue)
            seen += 1

    if seen == 0:
        sys.exit(f"{path}: no MLFQTRACE lines found -- was the kernel built "
                 f"with TRACE=1?")

    return series


def plot_timeline(series, out_path):
    plt.figure(figsize=(11, 5))

    last_tick = max(max(ticks) for ticks, _ in series.values())

    # Mark every priority boost. Labelled once so the legend stays readable.
    for n, boost in enumerate(range(BOOST_INTERVAL, last_tick + 1,
                                    BOOST_INTERVAL)):
        plt.axvline(boost, color="black", linestyle=":", linewidth=1,
                    alpha=0.45,
                    label="priority boost (every 48 ticks)" if n == 0 else None)

    # init is pid 1 and spends its life asleep; plotting it adds a flat line
    # at queue 0 that tells the reader nothing.
    pids = sorted(pid for pid in series if pid != 1)
    if not pids:
        sys.exit("trace only has init in it -- run schedulertest while "
                 "capturing")
    # tab20 pairs light and dark shades, so only use it once tab10 runs out
    palette = plt.cm.tab10 if len(pids) <= 10 else plt.cm.tab20
    colours = palette(range(len(pids)))

    # Processes sitting in the same queue would otherwise draw exactly on top
    # of each other and only the last one would be visible. Nudge each pid a
    # little off the gridline so they can all be seen; the offset is cosmetic
    # and always well under half a queue.
    spread = 0.28
    offsets = {pid: (i - (len(pids) - 1) / 2) * (spread / max(len(pids) - 1, 1))
               for i, pid in enumerate(pids)}

    for colour, pid in zip(colours, pids):
        ticks, queues = series[pid]
        nudged = [q + offsets[pid] for q in queues]

        # a sleeping process is not sampled, so only join samples from
        # consecutive ticks; a gap means it was out of the queuing network
        runs = [[0]]
        for i in range(1, len(ticks)):
            if ticks[i] - ticks[i - 1] <= 1:
                runs[-1].append(i)
            else:
                runs.append([i])
        for n, run in enumerate(runs):
            plt.step([ticks[i] for i in run], [nudged[i] for i in run],
                     where="post", color=colour, linewidth=1.6, alpha=0.85,
                     label=f"pid {pid}" if n == 0 else None)
        plt.scatter(ticks, nudged, color=colour, s=9, alpha=0.55)

    plt.yticks(range(NQUEUE), [f"Q{q}" for q in range(NQUEUE)])
    plt.ylim(NQUEUE - 0.5, -0.5)  # queue 0 (highest priority) on top
    plt.xlabel("Kernel tick (boosts land on multiples of 48)")
    plt.ylabel("Queue")
    plt.title("xv6 MLFQ: queue occupancy over time")
    plt.grid(axis="y", alpha=0.3)
    # below the axes, so it covers neither the data nor the watermark
    plt.legend(loc="upper center", bbox_to_anchor=(0.5, -0.13), fontsize=8,
               ncol=5, frameon=False)

    watermark()

    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    print(f"wrote {out_path}")


def plot_comparison(out_path):
    schedulers = list(COMPARISON)
    metrics = ["turnaround", "waiting", "response"]
    width = 0.25

    plt.figure(figsize=(8, 5))

    for n, metric in enumerate(metrics):
        positions = [i + (n - 1) * width for i in range(len(schedulers))]
        values = [COMPARISON[s][metric] for s in schedulers]
        bars = plt.bar(positions, values, width, label=metric.capitalize())
        for bar, value in zip(bars, values):
            plt.text(bar.get_x() + bar.get_width() / 2, value + 0.8,
                     f"{value:.2f}", ha="center", fontsize=8)

    plt.xticks(range(len(schedulers)), schedulers)
    plt.ylabel("Ticks (lower is better)")
    plt.title("Scheduler comparison: identical workload, single cpu")
    # Headroom plus a left-hand legend, so neither the bars nor the legend
    # box sit on top of the watermark in the top-right corner.
    tallest = max(v for s in COMPARISON.values() for v in s.values())
    plt.ylim(0, tallest * 1.22)
    plt.legend(loc="upper left")
    plt.grid(axis="y", alpha=0.3)

    watermark()

    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    print(f"wrote {out_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="?", default="trace.txt",
                        help="console capture from a TRACE=1 MLFQ kernel")
    parser.add_argument("-o", "--out-dir", default=".",
                        help="where to write the pngs")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    plot_comparison(os.path.join(args.out_dir, "scheduler_comparison.png"))

    if os.path.exists(args.trace):
        plot_timeline(read_trace(args.trace),
                      os.path.join(args.out_dir, "mlfq_timeline.png"))
    else:
        print(f"{args.trace}: not found, skipping the timeline plot",
              file=sys.stderr)


if __name__ == "__main__":
    main()
