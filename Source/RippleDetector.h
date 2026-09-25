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
#include "LaserTrigger.h"

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

    int rippleInputChannel { -1 }; // Global index of the ripple input channel

    // --- Noise channel veto ---
    int noiseInputChannel { -1 }; // Global index of the noise channel, -1 if none
    std::unique_ptr<NoiseVeto> noiseVeto; // Same method and settings, run on the noise channel
    std::vector<float> noiseFeatureScratch; // Noise channel feature values for the block
    std::atomic<uint32_t> vetoedOnsets { 0 }; // Onsets suppressed by the noise channel this run
    int rippleOutputChannel { 0 }; // Output TTL line for ripple events
    bool rippleTtlHigh { false }; // True while the ripple TTL line is high

    // --- Feature output (derived continuous channels) ---
    bool featureOutputRequested { false }; // Value of the "feature_out" parameter
    bool featureOutputActive { false }; // Derived channels exist for this stream
    ContinuousChannel* featureChannel { nullptr }; // RIP_FEAT: the RMS feature
    ContinuousChannel* thresholdChannel { nullptr }; // RIP_THR: baseline mean + x SD
    ContinuousChannel* eventChannelOut { nullptr }; // RIP_EVENT: threshold-height pulse while a ripple is detected

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
    std::vector<int> movementChannels; // Global indices of the channels used for movement detection
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

    /** True if the stream has a noise channel selected */
    bool hasNoiseChannel (uint16 streamId);

    /** Baseline statistics of the stream's noise channel (0 if none or still calibrating) */
    double getNoiseBaselineMean (uint16 streamId);
    double getNoiseBaselineStd (uint16 streamId);

    /** Ripple onsets vetoed by the noise channel since acquisition started */
    uint32_t getVetoedOnsets (uint16 streamId);

    /** Returns the viewer queue for a stream, or nullptr if the stream is unknown */
    FeatureFifo* getFeatureFifo (uint16 streamId);

    /** Returns the parameters currently applied to a stream's methods, or nullptr */
    const DetectionParams* getStreamParams (uint16 streamId);

    std::atomic<bool> shouldCalibrate { true };

    /** The laser trigger, for the editor's TEST button */
    LaserTrigger& getLaserTrigger() { return laserTrigger; }

private:
    StreamSettings<RippleDetectorSettings> settings;

    /** Fires the laser through the LaserDriver Pi's web API on every ripple onset (all streams) */
    LaserTrigger laserTrigger;

    /** Applies the laser_* parameters to laserTrigger; the destination only if it changed */
    void configureLaserTrigger (bool destinationChanged);

    /** Marks the current parameter values to be pushed to the stream's method */
    void applyParams (uint16 streamId);

    /** Asks the editor to refresh its custom controls */
    void refreshEditor();

    /** Adds the derived feature / threshold channels to a stream */
    void addFeatureChannels (DataStream* stream);

    /** Runs the method on the ripple channel, applies the noise veto and emits the events */
    void processRipples (uint16 streamId, const float* rippleData, const float* noiseData, int numSamples, int64 firstSample, float* featureOut, float* eventOut);

    /** Z-scores the block's features and hands them, with the raw ripple channel, to the viewer queue */
    void publishFeatures (uint16 streamId, const float* rippleData, int numSamples);

    /** Computes movement RMS windows for one block and updates pluginEnabled */
    void processMovement (uint16 streamId, AudioBuffer<float>& buffer, int numSamples, int64 firstSample);

    /** Emits a ripple TTL edge, tracking the line state */
    void setRippleTtl (uint16 streamId, bool state, int sampleIndex, int64 firstSample);

    /** Logs the baseline statistics after calibration */
    void logCalibration (uint16 streamId);

    void evalMovement (uint16 streamId, int64 firstSample);

    static double calculateRms (const float* data, int initIndex, int endIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RippleDetector);
};

#endif
