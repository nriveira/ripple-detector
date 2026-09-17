# Ripple Detector

![ripple-detector-screenshot](https://open-ephys.github.io/gui-docs/_images/rippledetector-01.png)

Open Ephys GUI plugin for ripple detection. It contains an embedded mechanism based on EMG or accelerometer data that blocks ripple events when movement is detected. 

## Installation

The plugin can be added via the Open Ephys GUI's built-in Plugin Installer. Press **ctrl-P** or **⌘P** to open the Plugin Installer, browse to "Ripple Detector", and click the "Install" button. The Ripple Detector plugin should now be available to use.

## Usage

Instructions for using the Ripple Detector plugin are available [here](https://open-ephys.github.io/gui-docs/User-Manual/Plugins/Ripple-Detector.html).

## Detection methods

The input channel is expected to be band-pass filtered in the ripple band (e.g. 150-250 Hz) upstream, for instance with the Bandpass Filter plugin. The **Method** drop-down selects how that signal is turned into ripple events. Every method starts with a 20 s calibration period (also triggered by the **CALIBRATE** button) during which a baseline mean and standard deviation are estimated; thresholds are expressed in baseline standard deviations.

| Method | Feature | Event onset | Event offset |
|---|---|---|---|
| **RMS** (default, original algorithm) | RMS of consecutive windows of `RMS Samples` samples | RMS above `Onset Std Dev` for longer than `Time Thresh.` | One RMS window after onset (fixed-length pulse) |
| **Envelope** | Rectified signal smoothed with a `Smoothing` ms moving average | Envelope above `Onset Std Dev` for `Time Thresh.` | Envelope below `Offset Std Dev`, or `Max Duration` reached |
| **TKEO** | Teager-Kaiser energy (`x[n]² − x[n−1]·x[n+1]`) smoothed with a `Smoothing` ms moving average | Energy above `Onset Std Dev` for `Time Thresh.` | Energy below `Offset Std Dev`, or `Max Duration` reached |

Notes:

- The Envelope and TKEO methods are causal, real-time versions of the classic offline approach (band-pass, envelope, z-score, onset/offset thresholds, minimum duration). Their TTL stays high for the whole event, so the falling edge marks the end of the ripple.
- TKEO scales with (amplitude × frequency)², which emphasises fast oscillations and suppresses slower components that leak through the band-pass filter.
- `Refrac. Time` is counted in samples from the event (from onset for RMS, from offset for Envelope/TKEO). The original plugin used wall-clock time, which drifted during File Reader playback.
- `Offset Std Dev` is clamped to `Onset Std Dev` so an event always ends.
- Every method is calibrated and computed continuously, so the method can be changed at any time, including during acquisition, without a new calibration.

**Baseline** selects how the baseline statistics evolve after calibration:

- *Fixed* (default): the calibration values are used until the next calibration.
- *Adaptive*: the mean and standard deviation keep tracking the signal outside of detected events with an exponential time constant of `Adapt Tau` seconds, so the threshold follows slow drifts in signal amplitude during long sessions.

Movement gating (EMG / accelerometer) works the same way for every method. When movement blocks detection, a ripple TTL that is currently high is forced low.

### Viewing the detection features

The plugin has a built-in viewer, opened with the tab / window buttons at the top right of the editor. It follows the stream selected in the editor and shows:

- the z-scored feature (in baseline standard deviations) of any method, chosen with the **Feature** drop-down: *Active method* follows the `Method` parameter, or pick RMS, Envelope or TKEO explicitly to compare them on the same data;
- the onset threshold (solid red) and, for Envelope / TKEO, the offset threshold (dashed orange);
- detected events (green), periods blocked by movement (orange) and the calibration period (grey);
- **Range** (vertical scale, in SD) and **Window** (2 to 30 s of history) drop-downs;
- the detection parameters of the stream in a panel on the right, plus a CALIBRATE button.

All three methods are computed on every block, each with its own baseline statistics, so the viewer can show any of them and switching the `Method` parameter takes effect immediately without a new calibration.

### Streaming the detection feature

With the **Features** toggle (parameter `feature_out`, off by default) the plugin appends three continuous channels to each stream, so the quantity being thresholded can be streamed to other plugins and recorded alongside the raw data:

| Channel | Contents |
|---|---|
| `RIP_FEAT` | The smoothed feature of the selected method: windowed RMS (held constant across each window), the rectified and smoothed envelope, or the smoothed Teager-Kaiser energy |
| `RIP_ON` | The current onset threshold (0 during calibration; follows the baseline in Adaptive mode) |
| `RIP_OFF` | The current offset threshold (equal to `RIP_ON` for the RMS method) |

The channels are typed as electrode channels and use the resolution (bit-volts) of the stream's first channel, so recordings keep the same scale as the input. Toggling the switch rebuilds the signal chain, which is why it is disabled during acquisition. Note that TKEO values have units of amplitude squared and are typically small, so they may need a smaller display range than the envelope or RMS.

### Adding a method

Detection algorithms live in `Source/DetectionMethods/` and have no dependency on JUCE or the GUI. To add one, subclass `DetectionMethod` (or `SampleFeatureMethod` for per-sample features with dual-threshold detection), register its name in `DetectionMethodFactory.h`, and add any new parameters in `RippleDetector::registerParameters()` and the editor.

### Testing the methods

`Tests/DetectionMethodTests.cpp` runs the methods over synthetic band-limited noise with ripple bursts, spike artefacts and amplitude drift. It needs only a C++17 compiler:

```bash
cmake -S Tests -B Tests/build && cmake --build Tests/build && ./Tests/build/detection_tests
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

