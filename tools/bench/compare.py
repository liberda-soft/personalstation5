#!/usr/bin/env python3
"""Summarize and compare benchmark logs written by `personalstation5_emulator --bench-log <dir>`.

    python3 tools/bench/compare.py runs/official runs/fork --skip 30 --duration 60

The first run is the baseline; later runs show their change against it. Only the Python 3
standard library is needed.
"""

import argparse
import csv
import math
import statistics
import sys
from pathlib import Path

# Debug counters summed over the measured window: (column, label, scale, unit).
COUNTER_SUMS = [
    ("ShadersCompiled", "Shaders compiled", 1, ""),
    ("ShaderCompileNs", "Shader compile time", 1e6, "ms"),
    ("PipelinesCreated", "Pipelines created", 1, ""),
    ("PipelineCreateNs", "Pipeline create time", 1e6, "ms"),
    ("GpuWaits", "CPU waits on GPU", 1, ""),
    ("GpuWaitNs", "CPU wait time on GPU", 1e6, "ms"),
    ("GpuFaults", "GPU write-tracking faults", 1, ""),
    ("QueueSubmits", "Queue submits", 1, ""),
    ("TextureEvictions", "Texture evictions", 1, ""),
    ("BufferEvictions", "Buffer evictions", 1, ""),
    ("TextureUploadBytes", "Texture uploads", 1 << 20, "MiB"),
    ("TextureDownloadBytes", "Texture downloads", 1 << 20, "MiB"),
    ("BufferUploadBytes", "Buffer uploads", 1 << 20, "MiB"),
    ("BufferDownloadBytes", "Buffer downloads", 1 << 20, "MiB"),
    ("LateVblanks", "Late vblanks", 1, ""),
    ("AudioUnderruns", "Audio underruns", 1, ""),
]

# Metrics where a lower value is better, for the change column.
LOWER_IS_BETTER = {
    "Avg frame time", "P99 frame time", "Max frame time", "Frames > 50 ms", "Frames > 100 ms",
    "Process CPU (avg)", "VRAM used (peak)",
} | {label for _, label, _, _ in COUNTER_SUMS}


def read_csv(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        return list(csv.DictReader(f))


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def percentile(sorted_values, fraction):
    if not sorted_values:
        return math.nan
    index = min(len(sorted_values) - 1, int(len(sorted_values) * fraction))
    return sorted_values[index]


def in_window(time_s, skip, duration):
    return time_s >= skip and (duration is None or time_s < skip + duration)


def summarize(run_dir, skip, duration):
    run_dir = Path(run_dir)
    frames = [
        number(row["frame_ms"])
        for row in read_csv(run_dir / "frametimes.csv")
        if in_window(number(row["time_s"]) or 0.0, skip, duration)
    ]
    frames = [ms for ms in frames if ms is not None and ms > 0.0]
    counters = [
        row for row in read_csv(run_dir / "counters.csv")
        if in_window(number(row["time_s"]) or 0.0, skip, duration)
    ]

    metrics = {}
    if frames:
        ordered = sorted(frames)
        seconds = sum(frames) / 1000.0
        metrics["Measured time"] = (seconds, "s")
        metrics["Frames"] = (len(frames), "")
        metrics["Avg FPS"] = (len(frames) / seconds, "")
        metrics["1% low FPS"] = (1000.0 / percentile(ordered, 0.99), "")
        metrics["0.1% low FPS"] = (1000.0 / percentile(ordered, 0.999), "")
        metrics["Avg frame time"] = (statistics.fmean(frames), "ms")
        metrics["P99 frame time"] = (percentile(ordered, 0.99), "ms")
        metrics["Max frame time"] = (ordered[-1], "ms")
        metrics["Frames > 50 ms"] = (sum(ms > 50.0 for ms in frames), "")
        metrics["Frames > 100 ms"] = (sum(ms > 100.0 for ms in frames), "")

    cpu = [v for v in (number(row.get("process_cpu_percent")) for row in counters) if v is not None]
    if cpu:
        metrics["Process CPU (avg)"] = (statistics.fmean(cpu), "%")
    vram = [v for v in (number(row.get("vram_used_mb")) for row in counters) if v is not None]
    if vram:
        metrics["VRAM used (peak)"] = (max(vram), "MiB")
    budget = [v for v in (number(row.get("vram_budget_mb")) for row in counters) if v is not None]
    if budget:
        metrics["VRAM budget"] = (max(budget), "MiB")
    for column, label, scale, unit in COUNTER_SUMS:
        values = [number(row.get(column)) for row in counters]
        values = [v for v in values if v is not None]
        if values:
            metrics[label] = (sum(values) / scale, unit)

    # Busiest threads by average share of one core over the window.
    threads = {}
    for row in counters:
        for i in range(1, 4):
            name = row.get(f"thread{i}_name")
            value = number(row.get(f"thread{i}_cpu_percent"))
            if name and value is not None:
                threads.setdefault(name, []).append(value)
    rows = max(len(counters), 1)
    busiest = sorted(((sum(v) / rows, name) for name, v in threads.items()), reverse=True)[:3]
    return metrics, busiest


def format_value(value, unit):
    if float(value).is_integer():
        text = f"{int(value)}"
    else:
        text = f"{value:.1f}"
    return f"{text} {unit}".strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runs", nargs="+", help="benchmark log folders; the first is the baseline")
    parser.add_argument("--skip", type=float, default=0.0, help="ignore the first N seconds (boot, loading)")
    parser.add_argument("--duration", type=float, default=None, help="measure only N seconds after --skip")
    args = parser.parse_args()

    summaries = []
    for run in args.runs:
        try:
            summaries.append(summarize(run, args.skip, args.duration))
        except FileNotFoundError as error:
            sys.exit(f"{run}: missing {Path(error.filename).name}")

    names = [Path(run).name or run for run in args.runs]
    labels = []
    for metrics, _ in summaries:
        labels += [label for label in metrics if label not in labels]

    header = ["Metric"] + names + ([f"Change ({name})" for name in names[1:]])
    table = [header]
    for label in labels:
        row = [label]
        base = summaries[0][0].get(label)
        for metrics, _ in summaries:
            value = metrics.get(label)
            row.append(format_value(*value) if value else "-")
        for metrics, _ in summaries[1:]:
            value = metrics.get(label)
            if not value or not base or base[0] == 0:
                row.append("")
                continue
            change = (value[0] - base[0]) / abs(base[0]) * 100.0
            better = change < 0 if label in LOWER_IS_BETTER else change > 0
            mark = "" if abs(change) < 2.0 else (" better" if better else " worse")
            row.append(f"{change:+.1f}%{mark}")
        table.append(row)

    widths = [max(len(row[i]) for row in table) for i in range(len(header))]
    for index, row in enumerate(table):
        print("  ".join(cell.ljust(widths[i]) for i, cell in enumerate(row)).rstrip())
        if index == 0:
            print("  ".join("-" * width for width in widths))

    print()
    for name, (_, busiest) in zip(names, summaries):
        threads = ", ".join(f"{thread} {cpu:.0f}%" for cpu, thread in busiest) or "n/a"
        print(f"Busiest threads ({name}): {threads}")


if __name__ == "__main__":
    main()
