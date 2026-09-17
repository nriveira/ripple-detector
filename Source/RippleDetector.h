#ifndef __RIPPLE_DETECTOR_H
#define __RIPPLE_DETECTOR_H

#include <ProcessorHeaders.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "DetectionMethods/DetectionMethod.h"

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
    String methodName; // Name of the selected detection method
    std::unique_ptr<DetectionMethod> method; // Algorithm instance for this stream
    DetectionParams params; // Parameters handed to the method
    std::atomic<bool> paramsDirty { false }; // params changed and must be pushed to the method
    std::vector<DetectionEvent> events; // Scratch buffer for events produced in the current block

    int rippleInputChannel { -1 }; // Global index of the ripple input channel
    int rippleOutputChannel { 0 }; // Output TTL line for ripple events
    bool rippleTtlHigh { false }; // True while the ripple TTL line is high

    // --- Calibration ---
    bool isCalibrating { true }; // Is in the calibration step
    bool calibrate { false }; // Per-stream request to recalibrate (e.g. method changed)
    int pointsProcessed { 0 }; // Samples processed during the current calibration
    int calibrationPoints { 0 }; // Samples that define the calibration duration

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

    /** Returns the name of the detection method selected for a stream */
    String getMethodName (uint16 streamId);

    std::atomic<bool> shouldCalibrate { true };

private:
    StreamSettings<RippleDetectorSettings> settings;

    /** Replaces the detection method for a stream and requests recalibration */
    void selectMethod (uint16 streamId, const String& methodName);

    /** Pushes the current parameter values to the stream's method */
    void applyParams (uint16 streamId);

    /** Asks the editor to show the parameters relevant for the current method */
    void refreshEditor();

    /** Handles the ripple channel for one block (calibration or detection) */
    void processRipples (uint16 streamId, const float* rippleData, int numSamples, int64 firstSample);

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
