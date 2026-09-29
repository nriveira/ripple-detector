#!/usr/bin/env python3
"""
Converts a WAV file (by default the pulse test audio) into an Open Ephys binary
recording that the GUI's File Reader can play back, so the Ripple Detector's pulse
test mode can be tried without a saline bath.

The WAV is resampled to the recording rate by linear interpolation and scaled so
that its peak becomes --pulse-uv microvolts, as if the pulses had been picked up by
an electrode. Optionally, noise, mains hum, the AC coupling of a sound card output
and a polarity inversion are added to make the playback more like a real saline
test. The result is one electrode channel, CH1, stored as 16-bit samples with the
Intan resolution of 0.195 uV per bit.

Output (a folder the File Reader opens through its structure.oebin):

    <out>/structure.oebin
    <out>/continuous/<stream>/continuous.dat

Standard library only:

    python3 Tools/wav_to_openephys.py
    python3 Tools/wav_to_openephys.py --highpass-hz 20 --invert --noise-uv 15
    python3 Tools/wav_to_openephys.py some_other.wav --out /tmp/other_recording
"""

import argparse
import array
import json
import math
import os
import random
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
RESOURCES = os.path.join(HERE, "..", "Resources")
DEFAULT_WAV = os.path.join(RESOURCES, "Test Audio", "pulse_test.wav")
DEFAULT_OUT = os.path.join(RESOURCES, "Test Recording")

STREAM = "PulseTest-100.0"  # continuous/<STREAM>/ ; also the stream's name in the GUI
BIT_VOLTS = 0.195  # uV per bit, as for Intan headstage channels
GUI_VERSION = "1.0.0"  # >= 0.6 selects the current binary layout in the File Reader


def read_wav(path):
    """Returns (samples in [-1, 1] from the first channel, sample rate)."""
    with wave.open(path, "rb") as w:
        channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(frames)

    if width == 2:
        data = array.array("h")
        data.frombytes(raw)
        if sys.byteorder == "big":
            data.byteswap()
        scale = 32768.0
    elif width == 1:  # 8-bit WAV is unsigned
        data = [b - 128 for b in raw]
        scale = 128.0
    elif width == 4:
        data = array.array("i")
        data.frombytes(raw)
        if sys.byteorder == "big":
            data.byteswap()
        scale = 2147483648.0
    else:
        raise SystemExit(f"{path}: unsupported sample width of {width} bytes")

    return [data[i] / scale for i in range(0, len(data), channels)], rate


def resample(x, rate_in, rate_out):
    """Linear interpolation onto the output rate's sample grid."""
    n_out = int(len(x) * rate_out / rate_in)
    last = len(x) - 1
    out = [0.0] * n_out
    step = rate_in / rate_out
    for i in range(n_out):
        t = i * step
        k = int(t)
        f = t - k
        out[i] = x[k] * (1.0 - f) + x[min(k + 1, last)] * f
    return out


def high_pass(x, rate, corner_hz):
    """First-order high-pass, like a sound card's output coupling capacitor."""
    rc = 1.0 / (2.0 * math.pi * corner_hz)
    a = rc / (rc + 1.0 / rate)
    prev_in = prev_out = 0.0
    out = [0.0] * len(x)
    for i, v in enumerate(x):
        prev_out = a * (prev_out + v - prev_in)
        prev_in = v
        out[i] = prev_out
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", nargs="?", default=DEFAULT_WAV, help="input WAV (default: the pulse test audio)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output folder (default: Resources/Test Recording)")
    ap.add_argument("--rate", type=int, default=30000, help="recording sample rate in Hz (default 30000)")
    ap.add_argument("--pulse-uv", type=float, default=1000.0,
                    help="the WAV's peak in microvolts after conversion (default 1000)")
    ap.add_argument("--noise-uv", type=float, default=10.0, help="Gaussian noise SD in uV (default 10)")
    ap.add_argument("--hum-uv", type=float, default=20.0, help="mains hum amplitude in uV (default 20)")
    ap.add_argument("--hum-hz", type=float, default=60.0, help="mains frequency in Hz (default 60)")
    ap.add_argument("--highpass-hz", type=float, default=0.0,
                    help="simulate a sound card's AC coupling with this corner (default 0: off)")
    ap.add_argument("--invert", action="store_true", help="invert the polarity, as many audio outputs do")
    ap.add_argument("--seed", type=int, default=1, help="random seed for the noise (default 1)")
    args = ap.parse_args()

    x, rate_in = read_wav(args.wav)
    peak = max((abs(v) for v in x), default=0.0)
    if peak == 0.0:
        raise SystemExit(f"{args.wav}: the audio is silent")

    y = resample(x, rate_in, args.rate)
    if args.highpass_hz > 0.0:
        y = high_pass(y, args.rate, args.highpass_hz)

    gain = (-1.0 if args.invert else 1.0) * args.pulse_uv / peak
    rng = random.Random(args.seed)
    w = 2.0 * math.pi * args.hum_hz / args.rate
    counts = array.array("h", bytes(2 * len(y)))
    clipped = 0
    for i, v in enumerate(y):
        uv = gain * v + rng.gauss(0.0, args.noise_uv) + args.hum_uv * math.sin(w * i)
        c = int(round(uv / BIT_VOLTS))
        if c > 32767 or c < -32768:
            clipped += 1
            c = max(-32768, min(32767, c))
        counts[i] = c
    if sys.byteorder == "big":
        counts.byteswap()  # the format is little-endian

    out = os.path.abspath(args.out)
    stream_dir = os.path.join(out, "continuous", STREAM)
    os.makedirs(stream_dir, exist_ok=True)
    with open(os.path.join(stream_dir, "continuous.dat"), "wb") as f:
        counts.tofile(f)

    source = os.path.basename(args.wav)
    oebin = {
        "GUI version": GUI_VERSION,
        "continuous": [{
            "folder_name": STREAM + "/",
            "sample_rate": float(args.rate),
            "source_processor_name": "Pulse Test",
            "source_processor_id": 100,
            "stream_name": "PulseTest",
            "recorded_processor": "Pulse Test",
            "recorded_processor_id": 100,
            "num_channels": 1,
            "channels": [{
                "channel_name": "CH1",
                "description": f"{source} converted by Tools/wav_to_openephys.py",
                "identifier": "genericdata.continuous",
                "history": "Pulse Test",
                "bit_volts": BIT_VOLTS,
                "units": "uV",
                "type": 0,  # electrode
            }],
        }],
        "events": [],
        "spikes": [],
    }
    with open(os.path.join(out, "structure.oebin"), "w") as f:
        json.dump(oebin, f, indent=3)

    print(f"{out}: {len(y)} samples ({len(y) / args.rate:.1f} s) at {args.rate} Hz, 1 channel, "
          f"peak {args.pulse_uv:g} uV{', inverted' if args.invert else ''}"
          f"{f', AC-coupled at {args.highpass_hz:g} Hz' if args.highpass_hz > 0 else ''}")
    if clipped:
        print(f"warning: {clipped} samples clipped to the 16-bit range; lower --pulse-uv")


if __name__ == "__main__":
    main()
