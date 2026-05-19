#!/usr/bin/env python3
"""
USB CDC control smoke test.

The firmware accepts the same JSON line protocol from USB CDC and BLE:
  {"cmd":"set_remote_mode","mode":"TAKEOVER"}
  {"cmd":"ros_control","seq":1,"gear":"D","throttle":0,"steer":-300,"buttons":0}

This script is intentionally small so it can be copied into a future ROS2 node.
Install dependency when needed:
  python3 -m pip install pyserial
"""

import argparse
import json
import time

import serial


def write_json(port, payload, read_seconds=0.25):
    line = json.dumps(payload, separators=(",", ":")) + "\n"
    port.write(line.encode("ascii"))
    port.flush()
    deadline = time.time() + read_seconds
    replies = []
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            break
        replies.append(raw.decode("utf-8", errors="replace").strip())
    return replies


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM11", help="COM11 on Windows or /dev/ttyACM0 on Linux")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--steer", type=int, default=-300, help="negative is right on current vehicle")
    parser.add_argument("--seconds", type=float, default=1.0)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=1.0) as port:
        time.sleep(0.25)
        for reply in write_json(port, {"cmd": "get_status"}):
            print(reply)

        for reply in write_json(port, {"cmd": "set_remote_mode", "mode": "TAKEOVER"}):
            print(reply)

        seq = 1
        end_time = time.time() + args.seconds
        while time.time() < end_time:
            for reply in write_json(
                port,
                {
                    "cmd": "ros_control",
                    "seq": seq,
                    "gear": "D",
                    "throttle": 0,
                    "steer": args.steer,
                    "aux_x": 0,
                    "aux_y": 0,
                    "buttons": 0,
                },
                read_seconds=0.05,
            ):
                print(reply)
            seq += 1
            time.sleep(0.05)

        for reply in write_json(
            port,
            {
                "cmd": "ros_control",
                "seq": seq,
                "gear": "D",
                "throttle": 0,
                "steer": 0,
                "aux_x": 0,
                "aux_y": 0,
                "buttons": 0,
            },
        ):
            print(reply)

        for reply in write_json(port, {"cmd": "set_remote_mode", "mode": "MONITOR"}):
            print(reply)


if __name__ == "__main__":
    main()
