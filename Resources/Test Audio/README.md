# Test pulses for pulse test mode

`pulse_test.wav` is a train of square test pulses for measuring the closed-loop latency with a known input instead of ripples. Play it into a saline bath, set the Ripple Detector's **Detect** to **Pulse (test)**, and each pulse is handled like a ripple: Ripple Output TTL, laser trigger and stimulation latency.

| File | Contents |
|---|---|
| `pulse_test.wav` | 60 s, 48 kHz, 16-bit mono. 24 pulses of 10 ms at half of full scale, 2 to 3 s apart (randomised), starting at 2 s. Loops cleanly |
| `pulse_test_onsets.csv` | Each pulse's onset time and sample number in the WAV |

The gaps are randomised so the pulses fall at every phase of the GUI's processing blocks, which gives the real spread of the latency rather than one phase of it. Every gap, including the one where a player loops back to the start, is longer than the default 1 s lockout.

## Setup

1. **Wiring.** Drive a wire in the saline from the sound card's output through a resistive divider, for example 100 kΩ in series and 100 Ω to the audio ground. That turns roughly 1 V from the headphone output into about 1 mV in the bath. Keep the pulse at the electrodes well inside the headstage's input range, about ±5 mV for Intan RHD amplifiers, and start with the player's volume low.
2. **Play** `pulse_test.wav` on loop.
3. **Ripple Input:** a channel that picks up the pulse. An unfiltered channel gives the cleanest edge. A band-pass-filtered channel still works, but the latency then includes the filter's delay.
4. **Detect:** Pulse (test). The ripple settings are replaced by **Pulse Thresh.** and **Lockout**.
5. **Pulse Thresh.:** open the viewer and set the threshold to roughly a third to half of the pulse height seen on the "Test pulse channel" trace. The pulse can appear with either polarity, which is fine.
6. **Lockout:** leave at 1000 ms. It hides the pulse's falling edge, the undershoot of the sound card's AC coupling, and any artefact of the stimulation, so each pulse is paired with exactly one stimulus.
7. **Stim. Input:** the digital input line that records the stimulus controller's trigger.

During acquisition, the viewer's side panel shows the pulses detected and the stimulation latency: mean, range, last value, and pulses with no stimulus. The same figures are written to the log when acquisition stops. The latency runs from the pulse's first sample above threshold to the next rising edge on Stim. Input, both on the recording's own sample clock.

## Without saline: File Reader playback

The same pulses are also available as an Open Ephys recording in [`Resources/Test Recording`](../Test%20Recording), for trying pulse test mode on any computer without a bath or headstage. It is one electrode channel, CH1, at 30 kHz: the WAV's pulses at 1000 µV peak, with 10 µV of noise and 20 µV of 60 Hz hum.

1. Add a **File Reader** and select `Resources/Test Recording/structure.oebin`.
2. Add the **Ripple Detector** after it, set **Detect** to **Pulse (test)** and **Pulse Thresh.** to about 300 µV.
3. Start acquisition. Each pulse gives one Ripple Output TTL, one sample after the pulse's edge, 10 ms long.

A file has no stimulus trigger, so the latency meter counts every pulse as *no stimulus*. If **Laser Trigger** is on, the laser fires on every pulse from the file, which is a way to exercise the laser path without saline.

`Tools/wav_to_openephys.py` makes the recording from the WAV, and can make it more like a real saline test:

```bash
python3 Tools/wav_to_openephys.py --highpass-hz 20 --invert --noise-uv 15
```

## Regenerating

`Tools/make_test_pulses.py` writes both files and needs only Python's standard library. For example, shorter and quieter pulses over two minutes:

```bash
python3 Tools/make_test_pulses.py --width-ms 5 --amplitude 0.25 --duration 120
```

Run it with `--help` for all options. Keep the shortest gap longer than the lockout, and the lockout longer than the pulse width. Afterwards, run `Tools/wav_to_openephys.py` to update the File Reader recording to match.
