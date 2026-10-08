#!/usr/bin/env python3
"""
Run on the LaserDriver Pi: logs every pulse the laser controller makes, and whether
a UDP trigger (the Ripple Detector's) started it.

The broker (laserhat-brokerd) reports a pulse_start event for every pulse,
whatever started it: a UDP trigger, the BNC trigger input, button 1, the web GUI
or a UART command. Its UDP counters only move for UDP triggers. So a pulse that
comes without a new UDP trigger was started by something else, and a pulse train
on the acquisition board's Stim. Input with no pulse_start here was not made by
the laser controller at all (e.g. its boot blink after a reset, which also shows
as mcu_alive going false, or noise on the TTL line).

    python3 laserhat_pulse_monitor.py                  # until Ctrl-C
    python3 laserhat_pulse_monitor.py --socket PATH    # another broker socket

Compare the summary with the plugin's "Laser (UDP): sent N" (viewer panel, and
the log when acquisition stops). Python 3 standard library only.
"""

import argparse
import json
import socket
import sys
import time

DEFAULT_SOCKET = "/run/laserhat/broker.sock"


class Broker:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buf = b""

    def send(self, msg):
        self.sock.sendall((json.dumps(msg) + "\n").encode())

    def lines(self):
        """Yields each JSON message the broker sends (events, state, replies)."""
        while True:
            while b"\n" not in self.buf:
                chunk = self.sock.recv(4096)
                if not chunk:
                    return
                self.buf += chunk
            line, self.buf = self.buf.split(b"\n", 1)
            try:
                yield json.loads(line)
            except ValueError:
                continue


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--socket", default=DEFAULT_SOCKET, help="broker JSON socket (default %(default)s)")
    args = ap.parse_args()

    try:
        broker = Broker(args.socket)
    except OSError as e:
        sys.exit(f"cannot connect to the broker at {args.socket}: {e} (is laserhat-broker running?)")

    t0 = time.time()
    counts = {"pulses": 0, "udp": 0, "other": 0, "resets": 0}
    last_udp_fired = None   # broker's UDP trigger count at the last stats reply
    last_pulse_time = None
    alive = None
    pending = None          # (time, gap) of a pulse waiting for its stats reply

    print(f"{'time s':>9}  {'since last':>10}  source", flush=True)
    broker.send({"cmd": "stats"})   # starting UDP counts

    try:
        for msg in broker.lines():
            kind = msg.get("type")
            now = time.time() - t0

            if kind == "event" and msg.get("event") == "pulse_start":
                # The UDP edge precedes the MCU's pulse_start (which comes over the UART),
                # so the stats asked for now already include this pulse's trigger if it was UDP
                counts["pulses"] += 1
                gap = "" if last_pulse_time is None else f"{(now - last_pulse_time) * 1000:8.1f} ms"
                last_pulse_time = now
                broker.send({"cmd": "stats"})
                pending = (now, gap)

            elif kind == "reply" and msg.get("cmd") == "stats":
                fired = int(msg.get("udp_fired", 0))
                if last_udp_fired is not None and pending is not None:
                    when, gap = pending
                    if fired > last_udp_fired:
                        counts["udp"] += 1
                        source = "UDP trigger"
                        if fired - last_udp_fired > 1:
                            source += f" ({fired - last_udp_fired} UDP triggers since the last pulse: the others hit a running pulse)"
                    else:
                        counts["other"] += 1
                        source = "NOT a UDP trigger (BNC input, button, web GUI or UART)"
                    print(f"{when:9.3f}  {gap:>10}  {source}", flush=True)
                    pending = None
                last_udp_fired = fired

            elif kind == "state" and "mcu_alive" in msg:
                if alive is not None and msg["mcu_alive"] != alive:
                    if not msg["mcu_alive"]:
                        counts["resets"] += 1
                    print(f"{now:9.3f}  {'':>10}  laser controller {'back' if msg['mcu_alive'] else 'LOST (reset or unplugged): its output blinks while it boots'}",
                          flush=True)
                alive = msg["mcu_alive"]
    except KeyboardInterrupt:
        pass

    print(f"\n{counts['pulses']} pulses: {counts['udp']} from UDP triggers, {counts['other']} from something else; "
          f"controller lost {counts['resets']} time(s)")


if __name__ == "__main__":
    main()
