#!/usr/bin/env python3
"""
Writes the test-pulse WAV for the Ripple Detector's pulse test mode, plus a CSV of
each pulse's onset time in the file.

Play the WAV on loop into saline (see "Resources/Test Audio/README.md"). Each square
pulse is detected on its leading edge like a ripple, and the stimulus that follows
is paired with it to measure the closed-loop latency.

The gaps between pulses are randomised (uniformly between --gap-min and --gap-max)
so the pulses fall at every phase of the GUI's processing blocks; regular pulses
could lock to one phase and hide the spread of the latency. The gaps must stay
longer than the detector's lockout (1 s by default). The file starts and ends with
enough silence that the gaps also hold where a looping player wraps around.

Standard library only:

    python3 Tools/make_test_pulses.py
    python3 Tools/make_test_pulses.py --width-ms 2 --amplitude 0.25 --duration 120

The pulses are 1 ms by default: when the stimulation is fed back into the bath,
its artefact is timed on the same channel, which needs the test pulse (and the
undershoot of the sound card's AC coupling) to be over before the stimulus
arrives. A 1 ms pulse leaves an undershoot of about 12% of its height.
"""

import argparse
import csv
import os
import random
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, "..", "Resources", "Test Audio", "pulse_test.wav")


def pulse_onsets(duration, width, gap_min, gap_max, lead_in, seed):
    """Onset times (s): first at lead_in; the last followed by at least gap_min - lead_in of silence."""
    rng = random.Random(seed)
    onsets = []
    t = lead_in
    # Looping: the silence after the last pulse plus the lead-in must be at least gap_min
    while t + width + max(0.0, gap_min - lead_in) <= duration:
        onsets.append(t)
        t += rng.uniform(gap_min, gap_max)
    return onsets


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=DEFAULT_OUT, help="WAV path (the CSV is written next to it)")
    ap.add_argument("--rate", type=int, default=48000, help="sample rate in Hz (default 48000)")
    ap.add_argument("--duration", type=float, default=60.0, help="length in s (default 60)")
    ap.add_argument("--width-ms", type=float, default=1.0, help="pulse width in ms (default 1)")
    ap.add_argument("--gap-min", type=float, default=2.0, help="shortest onset-to-onset gap in s (default 2)")
    ap.add_argument("--gap-max", type=float, default=3.0, help="longest onset-to-onset gap in s (default 3)")
    ap.add_argument("--lead-in", type=float, default=2.0, help="silence before the first pulse in s (default 2)")
    ap.add_argument("--amplitude", type=float, default=0.5, help="pulse height as a fraction of full scale (default 0.5)")
    ap.add_argument("--seed", type=int, default=1, help="random seed for the gaps (default 1)")
    args = ap.parse_args()

    if not 0.0 < args.amplitude <= 1.0:
        ap.error("--amplitude must be in (0, 1]")
    if args.gap_min <= args.width_ms / 1000.0 or args.gap_max < args.gap_min:
        ap.error("need width < gap-min <= gap-max")

    width = args.width_ms / 1000.0
    onsets = pulse_onsets(args.duration, width, args.gap_min, args.gap_max, args.lead_in, args.seed)

    n = int(round(args.duration * args.rate))
    level = int(round(args.amplitude * 32767))
    samples = bytearray(2 * n)  # 16-bit silence
    high = struct.pack("<h", level)
    width_samples = max(1, int(round(width * args.rate)))
    for t in onsets:
        start = int(round(t * args.rate))
        for i in range(start, min(n, start + width_samples)):
            samples[2 * i:2 * i + 2] = high

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(args.rate)
        w.writeframes(bytes(samples))

    csv_path = os.path.splitext(out)[0] + "_onsets.csv"
    with open(csv_path, "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["pulse", "onset_s", "sample"])
        for k, t in enumerate(onsets, 1):
            wr.writerow([k, f"{t:.6f}", int(round(t * args.rate))])

    gaps = [b - a for a, b in zip(onsets, onsets[1:])]
    print(f"{out}: {len(onsets)} pulses of {args.width_ms:g} ms at {args.amplitude:g} of full scale, "
          f"{args.duration:g} s at {args.rate} Hz mono")
    if gaps:
        wrap = args.duration - onsets[-1] + onsets[0]
        print(f"gaps {min(gaps):.2f}-{max(gaps):.2f} s; loop wrap gap {wrap:.2f} s")
    print(csv_path)


if __name__ == "__main__":
    main()
