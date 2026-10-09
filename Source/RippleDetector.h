#ifndef __RIPPLE_DETECTOR_H
#define __RIPPLE_DETECTOR_H

#include <ProcessorHeaders.h>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "DetectionMethods/DetectionMethod.h"
#include "DetectionMethods/NoiseVeto.h"
#include "FeatureFifo.h"
#include "DetectionMethods/PulseEdgeDetector.h"
#include "PeriodicSchedule.h"
#include "LaserTrigger.h"
#include "StimLatencyMeter.h"

class RippleDetectorEditor;

class RippleDetectorSettings
{
public:
    /** Constructor -- sets default values **/
    RippleDetectorSettings();

    /** Destructor **/
    ~RippleDetectorSettings() {}

    /** Creates an event associated with ripple detection */
    TTLEventPtr createEvent (int64 outputLine, int64 sample_number, bool state);

    // --- Ripple detection ---
    std::unique_ptr<DetectionMethod> method; // The RMS detection algorithm
    DetectionParams params; // Parameters handed to the method
    std::atomic<bool> paramsDirty { false }; // params changed and must be pushed to the method
    std::vector<DetectionEvent> events; // Scratch buffer for events produced in the current block
    std::vector<float> featureScratch; // Feature values for the block
    std::vector<float> zScratch; // Z-scored feature values for the block
    std::vector<uint8_t> flagScratch; // Per-sample status flags for the block
    FeatureFifo viewerFifo; // Hands features to the viewer

    int rippleInputChannel { -1 }; // Global index of the ripple input channel, -1 for None (nothing is detected)
    std::vector<float> silentScratch; // zeros standing in for the ripple channel when it is None

    // --- Noise channel veto ---
    int noiseInputChannel { -1 }; // Global index of the noise channel, -1 if none
    std::unique_ptr<NoiseVeto> noiseVeto; // Same method and settings, run on the noise channel
    std::vector<float> noiseFeatureScratch; // Noise channel feature values for the block
    std::atomic<uint32_t> vetoedOnsets { 0 }; // Onsets suppressed by the noise channel this run

    // --- Stimulation latency ---
    int stimInputLine { -1 }; // Digital input line carrying the stimulus controller's trigger, -1 if none
    StimLatencyMeter latency; // Pairs ripple onsets with that line's rising edges
    int rippleOutputChannel { 0 }; // Output TTL line for ripple events
    bool rippleTtlHigh { false }; // True while the ripple TTL line is high

    // --- Feature output (derived continuous channels) ---
    bool featureOutputRequested { false }; // Value of the "feature_out" parameter
    bool featureOutputActive { false }; // Derived channels exist for this stream
    ContinuousChannel* featureChannel { nullptr }; // RIP_FEAT: the RMS feature
    ContinuousChannel* thresholdChannel { nullptr }; // RIP_THR: baseline mean + x SD
    ContinuousChannel* eventChannelOut { nullptr }; // RIP_EVENT: threshold-height pulse while a ripple is detected

    // --- Test modes ---
    // Pulse (test): detect square test pulses on the Ripple Input channel; Periodic (test):
    // respond on a timer, ignoring the channel. Both respond as to a ripple (TTL, laser, latency).
    std::atomic<int> testModeRequested { 0 }; // "detect_mode": TEST_OFF, TEST_PULSE or TEST_PERIODIC (message thread)
    int testModeActive { 0 }; // audio thread
    PulseEdgeDetector pulse;
    PulseEdgeDetector::Params pulseParams; // written by parameterValueChanged, handed over with testParamsDirty
    PeriodicSchedule periodic;
    int periodicCount { 10 }; // "periodic_count"
    double periodicIntervalS { 2.0 }; // "periodic_interval"
    std::atomic<bool> testParamsDirty { false };
    bool testRestart { true }; // restart the detector / schedule at the next block (acquisition start, mode switch)
    std::vector<int> externalOnsets; // periodic onsets in this block
    std::vector<PulseEdgeDetector::Event> pulseEvents;
    std::atomic<uint32_t> testTriggered { 0 }; // responses this run
    std::atomic<uint32_t> testIgnored { 0 }; // pulses ignored in the lockout this run (detected pulses re-arm, so mostly 0)
    std::atomic<uint32_t> periodicSent { 0 };
    std::atomic<float> pulseNoiseSd { 0.0f }; // Pulse: the detector's noise estimate and threshold, for the viewer
    std::atomic<float> pulseThreshold { 0.0f };
    std::atomic<uint32_t> pulseRejected { 0 };
    std::atomic<bool> periodicRun { false }; // Periodic: START pressed and the sequence not finished (message thread sets it)
    bool periodicActive { false }; // audio thread: the schedule is running

    // --- Calibration ---
    bool isCalibrating { true }; // Is in the calibration step
    bool calibrate { false }; // Per-stream request to recalibrate
    int pointsProcessed { 0 }; // Samples processed during the current calibration
    int calibrationPoints { 0 }; // Samples that define the calibration duration
    std::atomic<float> calibrationProgress { 0.0f }; // 0..1 while calibrating, 1 afterwards (read by the UI)

    // --- Movement detection (EMG / accelerometer) ---
    String movSwitch { "OFF" }; // Movement detection mode (OFF / ACC / EMG)
    bool movSwitchEnabled { false }; // Movement gating is enabled
    bool movChannChanged { false }; // User selected new EMG/ACC channels
    std::vector<int> movementChannels; // Global indices of the channels used for movement detection (guarded by movLock)
    SpinLock movLock; // the message thread swaps movementChannels while the audio thread may be reading them
    uint16 movSourceStreamId { 0 }; // "mov_source": the stream the movement channels come from (0 = this stream)
    std::vector<int> movInputChannels; // "mov_input" (this stream), global indices

    // --- Movement view (the viewer's movement panel; shown whether or not movement gating is on) ---
    MovementFifo movFifo;
    StringArray movViewNames; // names of the traces in movFifo (message thread)
    float movViewRate { 0.0f }; // their rate after decimation, Hz (message thread)
    std::atomic<uint32_t> movViewVersion { 0 }; // changes whenever the traces change, so the viewer starts afresh
    int movDecimation { 1 }; // source samples per viewer sample
    int movDecimCount { 0 }; // audio thread: samples summed so far for the next viewer sample
    std::array<double, MovementFifo::MAX_TRACES> movDecimSum {};
    std::array<std::vector<float>, MovementFifo::MAX_TRACES> movViewScratch;
    int movementOutputChannel { 0 }; // Output TTL line for the movement-blocked state
    double movSds { 5.0 }; // SDs above the mean for the EMG/ACC threshold
    int minTimeWoMov { 5000 }; // Minimum immobility (ms) before detection is enabled again
    int minTimeWMov { 10 }; // Minimum movement (ms) before detection is disabled
    int minMovSamplesBelowThresh { 0 }; // minTimeWoMov in samples
    int minMovSamplesAboveThresh { 0 }; // minTimeWMov in samples
    unsigned int counterMovUpThresh { 0 }; // Samples with movement RMS above threshold
    unsigned int counterMovDownThresh { 0 }; // Samples with movement RMS below threshold
    bool flagMovMinTimeUp { false }; // Minimum movement time reached
    bool flagMovMinTimeDown { false }; // Minimum immobility time reached
    bool pluginEnabled { true }; // Ripple TTLs are allowed (no movement)

    BaselineStats movBaseline; // Movement RMS statistics from calibration
    std::vector<double> movRmsValues; // Movement RMS per window in the current block
    std::vector<int> movRmsNumSamples; // Window lengths matching movRmsValues
    std::vector<float> accMagnitude; // Scratch buffer for the acceleration magnitude

    double getMovThreshold() const { return movBaseline.getMean() + movSds * movBaseline.getStd(); }

    // TTL event channel
    EventChannel* eventChannel { nullptr };
};

class RippleDetector : public GenericProcessor
{
public:
    /** Constructor */
    RippleDetector();

    /** Destructor */
    ~RippleDetector() {}

    /** Creates the custom editor for this plugin */
    AudioProcessorEditor* createEditor() override;

    /** Creates and registers parameters */
    void registerParameters();

    /** Emits an event whenever a ripple is detected */
    void process (AudioBuffer<float>& buffer) override;

    /** Called when a processor needs to update its settings */
    void updateSettings() override;

    /** Called when a parameter is updated */
    void parameterValueChanged (Parameter* param) override;

    /** Clears the per-run noise veto counts */
    bool startAcquisition() override;

    /** Logs the noise veto counts and the laser trigger counts and request-to-reply times */
    bool stopAcquisition() override;

    /** Calibration progress of a stream: 0..1 while calibrating, 1 when done, -1 if the stream is unknown */
    float getCalibrationProgress (uint16 streamId);

    /** True while the stream is estimating its baseline */
    bool isCalibrating (uint16 streamId);

    /** Baseline statistics of a stream (0 if unknown or still calibrating) */
    double getBaselineMean (uint16 streamId);
    double getBaselineStd (uint16 streamId);

    /** False when the stream's Ripple Input is None */
    bool hasRippleInput (uint16 streamId);

    /** True if the stream has a noise channel selected */
    bool hasNoiseChannel (uint16 streamId);

    /** Baseline statistics of the stream's noise channel (0 if none or still calibrating) */
    double getNoiseBaselineMean (uint16 streamId);
    double getNoiseBaselineStd (uint16 streamId);

    /** Ripple onsets vetoed by the noise channel since acquisition started */
    uint32_t getVetoedOnsets (uint16 streamId);

    /** Digital input line of the stimulus trigger (-1 if none), and the latency measured against it this run */
    int getStimInputLine (uint16 streamId);
    StimLatencyMeter::Stats getLatencyStats (uint16 streamId);

    /** "PERIODIC START" / "PERIODIC STOP": the START / STOP button for every stream in Periodic (test),
        e.g. from a script through the HTTP API (PUT /api/processors/<id>/config or /api/message) */
    String handleConfigMessage (const String& msg) override;
    void handleBroadcastMessage (const String& msg, const int64 systemTimeMillis) override;

    /** Pairs the stimulus controller's hardware trigger with the ripple onsets */
    void handleTTLEvent (TTLEventPtr event) override;

    /** "detect_mode" values */
    static constexpr int TEST_OFF = 0; // ripple detection
    static constexpr int TEST_PULSE = 1; // Pulse (test)
    static constexpr int TEST_PERIODIC = 2; // Periodic (test)

    /** The stream's "detect_mode": TEST_OFF, TEST_PULSE or TEST_PERIODIC */
    int getTestMode (uint16 streamId);

    /** True in either test mode */
    bool isTestMode (uint16 streamId) { return getTestMode (streamId) != TEST_OFF; }

    struct TestStats
    {
        uint32_t triggered; // responses this run
        uint32_t periodicSent; // Periodic: responses sent this run
        double minStepUv; // Pulse: Min Step
        double thresholdSds; // Pulse: Pulse SDs
        double minWidthMs; // Pulse: Min Width
        double lockoutMs; // Pulse: Lockout
        float noiseSdUv; // Pulse: the channel's noise SD estimate now
        float thresholdNowUv; // Pulse: the threshold it gives (0 before the first estimate)
        uint32_t rejected; // Pulse: crossings shorter than Min Width
        int periodicCount;
        double periodicIntervalS;
    };

    /** The test modes' settings and counts for a stream */
    TestStats getTestStats (uint16 streamId);

    /** Periodic (test): START / STOP. A started sequence sends its first response at once and stops by itself
        after Pulses responses; it never starts on its own (acquisition start and mode switches leave it stopped). */
    void setPeriodicRunning (uint16 streamId, bool run);
    bool isPeriodicRunning (uint16 streamId);

    /** The movement panel's traces for a stream: their names (empty if none), rate and version */
    struct MovementView
    {
        StringArray names;
        float rate { 0.0f };
        uint32_t version { 0 };
        String sourceName; // the stream they come from
        bool gating { false }; // Mov. Detect is on, so they also block ripple detection
    };
    MovementView getMovementView (uint16 streamId);
    MovementFifo* getMovementFifo (uint16 streamId);

    /** The movement channels the Mov. Axes pop-up offers (the Mov. Source stream's), and the chosen names */
    StringArray getMovementSourceChannelNames (uint16 streamId);
    StringArray getMovementAxes (uint16 streamId);
    void setMovementAxes (uint16 streamId, const StringArray& names);

    /** Separator of the names in "mov_channels" */
    static constexpr const char* AXES_SEPARATOR = ",";

    /** Returns the viewer queue for a stream, or nullptr if the stream is unknown */
    FeatureFifo* getFeatureFifo (uint16 streamId);

    /** Returns the parameters currently applied to a stream's methods, or nullptr */
    const DetectionParams* getStreamParams (uint16 streamId);

    std::atomic<bool> shouldCalibrate { true };

    /** The laser trigger, for the editor's TEST button and the viewer's statistics */
    LaserTrigger& getLaserTrigger() { return laserTrigger; }
    const LaserTrigger& getLaserTrigger() const { return laserTrigger; }

private:
    StreamSettings<RippleDetectorSettings> settings;

    // True while updateSettings() runs. The editor must not be refreshed then: the
    // streams are being rebuilt, and the GUI refreshes the editor and viewer itself
    // once the update is complete (GenericEditor::update).
    bool updatingSettings { false };

    /** Fires the laser through the LaserDriver Pi's web API on every ripple onset (all streams) */
    LaserTrigger laserTrigger;

    /** Applies the laser_* parameters to laserTrigger; the destination only if it changed */
    void configureLaserTrigger (bool destinationChanged);

    /** Marks the current parameter values to be pushed to the stream's method */
    void applyParams (uint16 streamId);

    /** Streams that emitted an onset in the current process() call (for relayOnsets) */
    std::vector<uint16> firedStreams;

    /** Registers this call's onsets on every other stream with a Stim. Input line, on that stream's clock */
    void relayOnsets();

    /** Asks the editor to refresh its custom controls */
    void refreshEditor();

    /** Fills a channel drop-down ("ripple_channel" or "noise_channel") with the stream's channel
        names, optionally after "None", keeping the chosen channel by name */
    void setChannelChoices (const DataStream* stream, const String& paramName, bool withNone);

    /** Replaces a drop-down's entries, keeping the chosen entry by name (entry 0 stays put if it is a fixed choice) */
    static void setChoicesKeepingName (CategoricalParameter* param, const Array<String>& choices, bool firstIsFixed);

    /** Fills "mov_source" with the other streams' names */
    void setMovementSourceChoices (const DataStream* stream);

    /** Sets movementChannels from Mov. Input (this stream) or Mov. Axes (another stream), and the movement view */
    void updateMovementChannels (uint16 streamId);

    /** Hands the block's movement channels, decimated, to the viewer's movement panel */
    void publishMovement (uint16 streamId, AudioBuffer<float>& buffer);

    /** Applies a PERIODIC START / STOP message; returns how many streams it reached, or -1 if it is not one */
    int applyPeriodicMessage (const String& msg);

    /** Fills the viewer queue with silence and no flags (Ripple Input None) */
    void publishSilence (uint16 streamId, int numSamples);

    /** Adds the derived feature / threshold channels to a stream */
    void addFeatureChannels (DataStream* stream);

    /** Runs the method on the ripple channel, applies the noise veto and emits the events */
    void processRipples (uint16 streamId, const float* rippleData, const float* noiseData, int numSamples, int64 firstSample, float* featureOut, float* eventOut);

    /** Test modes: responds to detected test pulses or the periodic timer with the same outputs as a ripple */
    void processTestTrigger (uint16 streamId, const float* data, int numSamples, int64 firstSample,
                             float* featureOut, float* thresholdOut, float* eventOut);

    /** RIP_EVENT's level while a periodic test response's TTL is high (there is no threshold in that mode) */
    static constexpr float TEST_EVENT_LEVEL = 1000.0f;

    /** Z-scores the block's features and hands them, with the raw ripple channel, to the viewer queue */
    void publishFeatures (uint16 streamId, const float* rippleData, const float* noiseData, int numSamples);

    /** Computes movement RMS windows for one block and updates pluginEnabled */
    void processMovement (uint16 streamId, AudioBuffer<float>& buffer, int numSamples, int64 firstSample);

    /** Emits a ripple TTL edge, tracking the line state */
    void setRippleTtl (uint16 streamId, bool state, int sampleIndex, int64 firstSample);

    /** Logs the baseline statistics after calibration */
    void logCalibration (uint16 streamId);

    void evalMovement (uint16 streamId, int64 firstSample, int numSamples);

    static double calculateRms (const float* data, int initIndex, int endIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RippleDetector);
};

#endif
