from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path


CAPACITY = 256
SAMPLE = struct.Struct("<II8fI")
FIELDS = (
    "timestamp_ms",
    "response_count",
    "angle_degrees",
    "feedback_current_a",
    "raw_speed_rpm",
    "control_speed_rpm",
    "target_degrees",
    "speed_command_dps",
    "command_current_a",
    "integral_current_a",
    "flags",
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    parser.add_argument("--write-count", type=int, required=True)
    parser.add_argument("--valid-count", type=int, required=True)
    parser.add_argument("--csv", type=Path, default=Path("yaw_trace.csv"))
    args = parser.parse_args()

    data = args.dump.read_bytes()
    expected_size = CAPACITY * SAMPLE.size
    if len(data) != expected_size:
        raise SystemExit(f"dump size {len(data)} != {expected_size}")
    valid_count = min(max(args.valid_count, 0), CAPACITY)
    first = (args.write_count - valid_count) % CAPACITY
    rows = []
    for offset in range(valid_count):
        index = (first + offset) % CAPACITY
        rows.append(SAMPLE.unpack_from(data, index * SAMPLE.size))

    with args.csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(FIELDS)
        writer.writerows(rows)

    if not rows:
        print("no trace samples")
        return
    angles = [row[2] for row in rows]
    raw_speeds = [row[4] for row in rows]
    control_speeds = [row[5] for row in rows]
    currents = [row[8] for row in rows]
    feedback_steps = [
        rows[index][1] - rows[index - 1][1]
        for index in range(1, len(rows))
    ]
    print(f"samples={len(rows)} duration_ms={rows[-1][0] - rows[0][0]}")
    print(f"angle_deg={min(angles):.3f}..{max(angles):.3f}")
    print(f"raw_speed_rpm={min(raw_speeds):.3f}..{max(raw_speeds):.3f}")
    print(
        "control_speed_rpm="
        f"{min(control_speeds):.3f}..{max(control_speeds):.3f}"
    )
    print(f"command_current_a={min(currents):.4f}..{max(currents):.4f}")
    if feedback_steps:
        stale = sum(step == 0 for step in feedback_steps)
        print(f"stale_feedback_cycles={stale}/{len(feedback_steps)}")
    print(f"csv={args.csv}")


if __name__ == "__main__":
    main()
