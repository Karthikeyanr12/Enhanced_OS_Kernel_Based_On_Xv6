#!/usr/bin/env python3
"""
scripts/parse_benchmark.py
Parses benchmark output from xv6 benchmark programs and outputs structured Markdown and CSV tables.
"""

import sys

def parse_lines(lines):
    records = []
    for line in lines:
        line = line.strip()
        if line.startswith("CSV:"):
            parts = [p.strip() for p in line[4:].split(",")]
            if len(parts) >= 8:
                records.append({
                    "type": parts[0],
                    "pid": parts[1],
                    "prio": parts[2],
                    "ticks": parts[3],
                    "sched": parts[4],
                    "resp": parts[5],
                    "wait": parts[6],
                    "turn": parts[7],
                    "scall": parts[8] if len(parts) > 8 else "0"
                })
    return records

def print_markdown_table(records):
    print("| Workload Type | PID | Final Priority | CPU Ticks | Schedules | Response Time | Waiting Time | Turnaround Time | Syscalls |")
    print("|---------------|-----|----------------|-----------|-----------|---------------|--------------|-----------------|----------|")
    for r in records:
        print(f"| {r['type']} | {r['pid']} | Q{r['prio']} | {r['ticks']} | {r['sched']} | {r['resp']} ticks | {r['wait']} ticks | {r['turn']} ticks | {r['scall']} |")

def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], "r") as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()

    records = parse_lines(lines)
    if not records:
        print("No CSV records found in input.")
        return

    print("### Parsed Benchmark Results (Markdown Table)")
    print_markdown_table(records)
    print("\n### Raw CSV")
    print("Type,PID,Priority,CPU_Ticks,Num_Sched,Response_Time,Wait_Time,Turnaround_Time,Syscalls")
    for r in records:
        print(f"{r['type']},{r['pid']},Q{r['prio']},{r['ticks']},{r['sched']},{r['resp']},{r['wait']},{r['turn']},{r['scall']}")

if __name__ == "__main__":
    main()
