# Test pulses for pulse test mode

`pulse_test.wav` is a train of square test pulses for testing the closed loop with a known input instead of ripples. Play it into a saline bath, or play the same pulses from the File Reader recording below, and set the Ripple Detector's **Detect** to **Pulse (test)**. Each pulse is then handled like a ripple: Ripple Output TTL, laser trigger and stimulation latency.

| File | Contents |
|---|---|
| `pulse_test.wav` | 60 s, 48 kHz, 16-bit mono. 24 pulses of 1 ms at half of full scale, 2 to 3 s apart (randomised), starting at 2 s. Loops cleanly |
| `pulse_test_onsets.csv` | Each pulse's onset time and sample number in the WAV |

The pulses are 1 ms long so that each is over before the stimulation arrives, which keeps the stimulation's artefact separate from the pulse for offline analysis. The gaps are randomised so the pulses fall at every phase of the GUI's processing blocks, which gives the real spread of the latency rather than one phase of it. Every gap, including the one where a player loops back to the start, is longer than the default 1 s lockout.

## Setup

1. **Wiring.** Drive a wire in the saline from the sound card's output through a resistive divider, for example 100 kΩ in series and 100 Ω to the audio ground. That turns roughly 1 V from the headphone output into about 1 mV in the bath. Keep the pulse at the electrodes well inside the headstage's input range, about ±5 mV for Intan RHD amplifiers, and start with the player's volume low.
2. **Play** `pulse_test.wav` on loop.
3. **Ripple Input:** a channel that picks up the pulse. An unfiltered channel gives the cleanest edge. A band-pass-filtered channel still works, but the latency then includes the filter's delay.
4. **Detect:** Pulse (test). The ripple settings are replaced by **Pulse SDs**, **Min Step**, **Min Width** and **Lockout**. Turn on **Laser Trigger** so each detection triggers the LaserHAT.
5. **Threshold:** the defaults (8 noise SDs, at least 100 µV, held for 0.3 ms) follow the noise on the channel, so usually nothing needs setting. Open the viewer and check that the pulses rise well above the threshold line, and that the side panel's noise SD and threshold look right. Only rising edges count. If the pulse appears upside down on the "Test pulse channel" trace, swap the two wires into the bath. If noise still triggers responses, raise Pulse SDs or Min Width (keep Min Width under the 1 ms pulse).
6. **Lockout:** leave at 1000 ms. It hides the pulse's falling edge, the undershoot of the sound card's AC coupling, and any artefact of the stimulation, so each pulse is paired with exactly one stimulus.
7. **Stimulus TTL:** wire the LaserHAT's output TTL into one of the acquisition board's digital inputs, and select that line as **Stim. Input**.
8. **Stimulation feedback** (optional): feed a small stimulation back into the same bath, so its artefact is recorded on the probe a few milliseconds after each test pulse, for offline analysis. Keep it within the headstage's input range too.

During acquisition, the viewer marks each output TTL, and its side panel shows the pulses detected and the stimulation latency: mean, range, last value, and pulses without a stimulus. The same figures are written to the log when acquisition stops. The latency runs from the test pulse's first sample above threshold to the LaserHAT's TTL arriving at the board, both on the board's sample clock. The artefact on the probe, recorded alongside, gives the delivered stimulus's timing offline.

## Without saline: File Reader playback

The same pulses are also available as an Open Ephys recording in [`Resources/Test Recording`](../Test%20Recording), for trying pulse test mode on any computer without a bath or headstage. It is one electrode channel, CH1, at 30 kHz: the WAV's 1 ms pulses at 1000 µV peak, with 10 µV of noise and 20 µV of 60 Hz hum.

1. Add a **File Reader** and select `Resources/Test Recording/structure.oebin`.
2. Add the **Ripple Detector** after it, set **Detect** to **Pulse (test)** (the default thresholds suit the recording), and turn on **Laser Trigger**.
3. Start acquisition. Each pulse gives one Ripple Output TTL, Min Width (0.3 ms) after the pulse's edge, 10 ms long, and one trigger to the LaserHAT.

To record the LaserHAT's output, run the acquisition board as a second source (merged into the same chain) with the LaserHAT's TTL on one of its digital inputs, and set **Stim. Input** to that line on the board's stream and to none on the File Reader's. The file and the board run on separate clocks, so the latency is then measured from the moment each trigger is sent to the TTL's arrival at the board (see "Stimulus TTL on another stream" in the main README).

`Tools/wav_to_openephys.py` makes the recording from the WAV, and can make it more like a real saline test:

```bash
python3 Tools/wav_to_openephys.py --highpass-hz 20 --noise-uv 15
```

(`--invert` writes the pulses upside down; the detector only takes rising edges, so it will not respond to such a recording.)

To test an offline analysis of the stimulation artefact without hardware, add a simulated artefact a known time after every pulse, and write it to a separate folder:

```bash
python3 Tools/wav_to_openephys.py --highpass-hz 20 --stim-delay-ms 7.5 --out "Resources/Test Recording (simulated stimulus)"
```

## Regenerating

`Tools/make_test_pulses.py` writes both files and needs only Python's standard library. For example, quieter pulses over two minutes:

```bash
python3 Tools/make_test_pulses.py --amplitude 0.25 --duration 120
```

Run it with `--help` for all options. Keep the shortest gap longer than the lockout, and the lockout longer than the pulse width. Afterwards, run `Tools/wav_to_openephys.py` to update the File Reader recording to match.
