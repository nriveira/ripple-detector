# Ripple Detector

![ripple-detector-screenshot](https://open-ephys.github.io/gui-docs/_images/rippledetector-01.png)

Open Ephys GUI plugin for ripple detection. It contains an embedded mechanism based on EMG or accelerometer data that blocks ripple events when movement is detected. 

## Installation

The plugin can be added via the Open Ephys GUI's built-in Plugin Installer. Press **ctrl-P** or **⌘P** to open the Plugin Installer, browse to "Ripple Detector", and click the "Install" button. The Ripple Detector plugin should now be available to use.

## Usage

Instructions for using the Ripple Detector plugin are available [here](https://open-ephys.github.io/gui-docs/User-Manual/Plugins/Ripple-Detector.html).

## Detection method

The input channel is expected to be band-pass filtered in the ripple band (e.g. 150-250 Hz) upstream, for instance with the Bandpass Filter plugin. Detection uses the original windowed-RMS algorithm:

1. The signal is split into consecutive windows of `RMS Samples` samples and the RMS of each window is computed.
2. During a 20 s **calibration** the RMS values are collected and their mean and standard deviation (SD) are estimated. The detection threshold is *mean + `Onset Std Dev` x SD*. No ripples are reported while calibrating.
3. Once the RMS has stayed above the threshold for longer than `Time Thresh.`, a TTL pulse one window long is emitted on the `Ripple Output` line and a refractory period of `Refrac. Time` starts.

Calibration runs automatically when acquisition starts and when the movement channels change. The **CALIBRATE** button (editor and viewer) restarts it, shows the progress while it runs, and has a tooltip describing the procedure. The viewer also prints the resulting baseline mean, SD and threshold.

Notes:

- `Refrac. Time` is counted in samples from the event onset. The original plugin used wall-clock time, which drifted during File Reader playback.
- Envelope and TKEO detection methods exist in `Source/DetectionMethods/` and are unit-tested, but are not exposed in the plugin at the moment.

**Baseline** selects how the baseline statistics evolve after calibration:

- *Fixed* (default): the calibration values are used until the next calibration.
- *Adaptive*: the mean and standard deviation keep tracking the signal outside of detected events with an exponential time constant of `Adapt Tau` seconds, so the threshold follows slow drifts in signal amplitude during long sessions.

Movement gating (EMG / accelerometer) blocks ripple events while movement is detected. When movement blocks detection, a ripple TTL that is currently high is forced low.

### Viewing the detection feature

The plugin has a built-in viewer, opened with the tab / window buttons at the top right of the editor. It follows the stream selected in the editor and shows:

- the RMS feature in baseline standard deviations, so the detection threshold is a horizontal line at `Onset Std Dev`;
- detected events (green), periods blocked by movement (orange) and the calibration period (grey);
- **Range** (vertical scale, in SD) and **Window** (2 to 30 s of history) drop-downs;
- the detection parameters of the stream, a CALIBRATE button with progress, and the current baseline statistics in a panel on the right.

### Streaming the detection feature

With the **Features** toggle (parameter `feature_out`) the plugin appends three continuous channels to each stream, so the quantity being thresholded can be watched in the LFP Viewer next to the raw traces, streamed to other plugins, and recorded alongside the raw data:

| Channel | Contents |
|---|---|
| `RIP_FEAT` | The windowed RMS of the ripple input channel (held constant across each window) |
| `RIP_THR` | The detection threshold, *baseline mean + `Onset Std Dev` x SD* (0 during calibration; follows the baseline in Adaptive mode). The feature crossing this line is what triggers detection |
| `RIP_EVENT` | Equal to the threshold while a ripple is detected (the TTL line is high), 0 otherwise |

The channels are typed as electrode channels and use the resolution (bit-volts) of the stream's first channel, so recordings keep the same scale as the input. Toggling the switch rebuilds the signal chain, which is why it is disabled during acquisition.

### Closed-loop laser trigger

With **Laser Trigger** on, every ripple onset also sends one UDP datagram to the [LaserDriver](https://github.com/nriveira/LaserDriver) Pi, whose broker fires the laser's fast GPIO trigger. The datagram is sent from the same processing block as the TTL event, before the event is added, so the network path adds nothing beyond one `sendto()`.

| Parameter | Meaning |
|---|---|
| `Laser Trigger` | On/off; can be switched during acquisition |
| `Laser Host` | Numeric IPv4 address of the Pi (no host names, so sending never waits on a lookup) |
| `Laser Port` | The broker's `--udp-trigger-port` (default 27136) |

The settings apply to every stream. On the Pi, start the broker with `--udp-trigger-port 27136 --udp-trigger-allow <this computer's IP>` (see LaserDriver `Pi/README.md`).

Each datagram is 16 bytes, little-endian: `"LTR1"`, a `u32` sequence number and the `u64` sample number of the onset event. The broker counts gaps in the sequence, so a lost trigger shows up in its `udp_stats`. The layout is in `Source/LaserTriggerPacket.h` and is mirrored by LaserDriver `Pi/udp_trigger.py`, and both test suites check the same byte vector.

The onset sample number is the start of the RMS window that crossed threshold, while the datagram leaves at the end of the processing block that contains it. The difference, plus the GUI's block latency, is the host-side part of the stimulation delay.

### Adding a method

Detection algorithms live in `Source/DetectionMethods/` and have no dependency on JUCE or the GUI. To add one, subclass `DetectionMethod` (or `SampleFeatureMethod` for per-sample features with dual-threshold detection), register its name in `DetectionMethodFactory.h`, and add any new parameters in `RippleDetector::registerParameters()` and the editor.

### Testing the methods

`Tests/DetectionMethodTests.cpp` runs the methods over synthetic band-limited noise with ripple bursts, spike artefacts and amplitude drift. `Tests/LaserTriggerPacketTests.cpp` checks the laser trigger datagram layout. Both need only a C++17 compiler:

```bash
cmake -S Tests -B Tests/build && cmake --build Tests/build && ./Tests/build/detection_tests && ./Tests/build/laser_trigger_tests
```

## Building from source

First, follow the instructions on [this page](https://open-ephys.github.io/gui-docs/Developer-Guide/Compiling-the-GUI.html) to build the Open Ephys GUI.

Then, clone this repository into a directory at the same level as the `plugin-GUI`, e.g.:
 
```
Code
├── plugin-GUI
│   ├── Build
│   ├── Source
│   └── ...
├── OEPlugins
│   └── ripple-detector
│       ├── Build
│       ├── Source
│       └── ...
```

### Windows

**Requirements:** [Visual Studio](https://visualstudio.microsoft.com/) and [CMake](https://cmake.org/install/)

From the `Build` directory, enter:

```bash
cmake -G "Visual Studio 17 2022" -A x64 ..
```

Next, launch Visual Studio and open the `OE_PLUGIN_ripple-detector.sln` file that was just created. Select the appropriate configuration (Debug/Release) and build the solution.

Selecting the `INSTALL` project and manually building it will copy the `.dll` and any other required files into the GUI's `plugins` directory. The next time you launch the GUI from Visual Studio, the Ripple Detector plugin should be available.


### Linux

**Requirements:** [CMake](https://cmake.org/install/)

From the `Build` directory, enter:

```bash
cmake -G "Unix Makefiles" ..
make install
```

This will build the plugin and copy the `.so` file into the GUI's `plugins` directory. The next time you launch the GUI compiled version of the GUI, the Ripple Detector plugin should be available.


### macOS

**Requirements:** [Xcode](https://developer.apple.com/xcode/) and [CMake](https://cmake.org/install/)

From the `Build` directory, enter:

```bash
cmake -G "Xcode" ..
```

Next, launch Xcode and open the `ripple-detector.xcodeproj` file that now lives in the “Build” directory.

Running the `ALL_BUILD` scheme will compile the plugin; running the `INSTALL` scheme will install the `.bundle` file to `/Users/<username>/Library/Application Support/open-ephys/plugins-api10`. The Ripple Detector plugin should be available the next time you launch the GUI from Xcode.

## Attribution

If you want to cite the ripple detector or know more about it, please refer to the paper below:

https://iopscience.iop.org/article/10.1088/1741-2552/ac857b

## References

Drieu, C., Todorova, R., & Zugaro, M. (2018a). Nested sequences of hippocampal assemblies during behavior support subsequent sleep replay. Science (New York, N.Y.), 362(6415), 675–679. https://doi.org/10.1126/science.aat2952

Drieu, C., Todorova, R., & Zugaro, M. (2018b). Bilateral recordings from dorsal hippocampal area CA1 from rats transported on a model train and sleeping. CRCNS.org. http://dx.doi.org/10.6080/K0Z899MM.

