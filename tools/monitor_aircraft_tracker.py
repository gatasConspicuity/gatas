#!/usr/bin/env python3
"""Log GatasConnect UDP and AircraftTracker counters for correlation."""

import argparse
import csv
import datetime as dt
import json
import time
import urllib.error
import urllib.request


def read_json(url: str, timeout: float):
    started = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            body = response.read()
        return {
            "ok": True,
            "body_bytes": len(body),
            "elapsed_ms": round((time.monotonic() - started) * 1000.0, 1),
            "data": json.loads(body),
            "error": "",
        }
    except (OSError, urllib.error.URLError, json.JSONDecodeError) as error:
        return {
            "ok": False,
            "body_bytes": 0,
            "elapsed_ms": round((time.monotonic() - started) * 1000.0, 1),
            "data": {},
            "error": str(error),
        }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.178.227")
    parser.add_argument("--duration", type=float, default=300.0)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--output", default="aircraft_tracker_monitor.csv")
    args = parser.parse_args()

    udp_url = f"http://{args.host}/api/GatasConnectUDP.json"
    tracker_url = f"http://{args.host}/api/AircraftTracker.json"
    fields = [
        "utc",
        "elapsed_s",
        "udp_ok",
        "udp_body_bytes",
        "udp_elapsed_ms",
        "udp_pkg_received",
        "udp_pkg_received_delta",
        "udp_bytes_received_kb",
        "udp_bytes_received_kb_delta",
        "udp_has_connection",
        "tracker_ok",
        "tracker_body_bytes",
        "tracker_elapsed_ms",
        "tracker_count",
        "tracker_processed",
        "tracker_processed_delta",
        "tracker_queue_full",
        "error",
    ]

    previous_udp = {}
    previous_tracker = {}
    started = time.monotonic()
    deadline = started + args.duration

    with open(args.output, "w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        while time.monotonic() < deadline:
            sample_started = time.monotonic()
            udp = read_json(udp_url, args.timeout)
            tracker = read_json(tracker_url, args.timeout)
            udp_data = udp["data"]
            tracker_data = tracker["data"]
            udp_packets = udp_data.get("pkgReceived:k", "")
            udp_bytes = udp_data.get("bytesReceived:kb", "")
            processed = tracker_data.get("positionsProcessed:k", "")

            def delta(value, previous):
                if not isinstance(value, int) or not isinstance(previous, int):
                    return ""
                return value - previous

            row = {
                "utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
                "elapsed_s": round(time.monotonic() - started, 1),
                "udp_ok": udp["ok"],
                "udp_body_bytes": udp["body_bytes"],
                "udp_elapsed_ms": udp["elapsed_ms"],
                "udp_pkg_received": udp_packets,
                "udp_pkg_received_delta": delta(udp_packets, previous_udp.get("packets")),
                "udp_bytes_received_kb": udp_bytes,
                "udp_bytes_received_kb_delta": delta(udp_bytes, previous_udp.get("bytes")),
                "udp_has_connection": udp_data.get("hasConnection:b", ""),
                "tracker_ok": tracker["ok"],
                "tracker_body_bytes": tracker["body_bytes"],
                "tracker_elapsed_ms": tracker["elapsed_ms"],
                "tracker_count": tracker_data.get("numberOfObjectsTracking", ""),
                "tracker_processed": processed,
                "tracker_processed_delta": delta(processed, previous_tracker.get("processed")),
                "tracker_queue_full": tracker_data.get("queueFull:err", ""),
                "error": "; ".join(error for error in (udp["error"], tracker["error"]) if error),
            }
            writer.writerow(row)
            output.flush()
            print(",".join(str(row[field]) for field in fields), flush=True)
            previous_udp = {"packets": udp_packets, "bytes": udp_bytes}
            previous_tracker = {"processed": processed}
            remaining = args.interval - (time.monotonic() - sample_started)
            if remaining > 0:
                time.sleep(remaining)


if __name__ == "__main__":
    main()
