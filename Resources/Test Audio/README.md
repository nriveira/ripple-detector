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

## Regenerating

`Tools/make_test_pulses.py` writes both files and needs only Python's standard library. For example, shorter and quieter pulses over two minutes:

```bash
python3 Tools/make_test_pulses.py --width-ms 5 --amplitude 0.25 --duration 120
```

Run it with `--help` for all options. Keep the shortest gap longer than the lockout, and the lockout longer than the pulse width.
