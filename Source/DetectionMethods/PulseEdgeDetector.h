#ifndef __PULSE_EDGE_DETECTOR_H
#define __PULSE_EDGE_DETECTOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

/**
    Debug detector: finds the leading edge of a square pulse on one channel, for
    measuring the closed-loop latency with a known input (e.g. test pulses played
    into saline) instead of ripples.

    Each sample is compared with a slowly tracking baseline. An edge is found when
    |x - baseline| stays above the threshold for CONFIRM_SAMPLES consecutive
    samples, so either polarity works (sound-card outputs often invert) and a
    single-sample glitch is ignored. The edge time is the first sample above the
    threshold; the decision is the sample that confirmed it.

    After an edge, detection is locked out for lockoutMs. That hides the pulse's
    own falling edge, the undershoot of an AC-coupled source and any artefact of
    the stimulation that follows, so each input pulse is paired with exactly one
    response. Detection re-arms only once the signal is back near its baseline,
    so a pulse still high when the lockout ends is not detected again then. Its
    falling edge does count as a new edge if it comes after the lockout, so the
    lockout should be longer than the test pulses.

    The baseline is frozen during the lockout and tracks the signal otherwise.

    The stimulation that follows a pulse is not analysed here: it returns as a TTL
    (Stim. Input) and its artefact on the channel is analysed offline.

    External onsets: process() can instead be given the onsets (e.g. from a
    periodic schedule). Each starts a pulse exactly as a detected edge would
    (output TTL, lockout), onsets during the lockout are ignored and counted, and
    the channel's own threshold crossings are not used.

    Free of JUCE so Tests/ can drive it. Not thread-safe: setParams() and
    process() are called from the audio thread.
*/
class PulseEdgeDetector
{
public:
    struct Params
    {
        float sampleRate { 30000.0f };
        double thresholdUv { 200.0 }; // |x - baseline| that counts as an edge, in the channel's units
        double lockoutMs { 1000.0 }; // no new edge for this long after one
        double ttlMs { 10.0 }; // how long the output TTL stays high after an edge
    };

    /** Something that happened in the current block */
    struct Event
    {
        enum Type
        {
            Onset, // a pulse edge: the output TTL goes high
            TtlOff // the output TTL goes low
        };

        int sampleIndex; // the sample that confirmed it (Onset) or the TTL's end (TtlOff)
        Type type;
        int edgeOffset; // Onset: samples from the first threshold crossing back to sampleIndex (>= 0; 0 for external onsets)
    };

    static constexpr int CONFIRM_SAMPLES = 2; // consecutive samples above threshold
    static constexpr double BASELINE_TAU_MS = 20.0; // baseline tracking time constant
    static constexpr double REARM_FRACTION = 0.5; // after the lockout, re-arm below this share of the threshold

    void setParams (const Params& p);
    const Params& getParams() const { return params; }

    /** Clears all state; the next sample starts the baseline */
    void reset();

    /**
        Runs one block, appending TTL edges to 'events'. Optional outputs per sample:
        featureOut receives |x - baseline|; lockoutOut is 1 while detection is locked
        out or waiting to re-arm, 0 while armed.

        externalOnsets (optional, sorted block-relative sample indices): the onsets
        come from elsewhere and the channel's crossings are not used; see above.
    */
    void process (const float* data, int numSamples, std::vector<Event>& events,
                  float* featureOut = nullptr, uint8_t* lockoutOut = nullptr,
                  const std::vector<int>* externalOnsets = nullptr);

    bool isLockedOut() const { return lockoutLeft > 0; }
    bool isTtlHigh() const { return ttlLeft > 0; }
    uint32_t getDetections() const { return detections; }
    uint32_t getIgnoredExternal() const { return ignoredExternal; } // external onsets that fell in a lockout

private:
    Params params;

    int64_t lockoutSamples { 30000 };
    int64_t ttlSamples { 300 };
    double alpha { 0.0 }; // baseline update weight per sample

    int64_t sampleCount { 0 }; // samples since reset(), for latencies across blocks

    bool started { false };
    double baseline { 0.0 };
    bool armed { true };
    int above { 0 }; // consecutive samples above threshold
    int64_t lockoutLeft { 0 };
    int64_t ttlLeft { 0 };
    uint32_t detections { 0 };
    uint32_t ignoredExternal { 0 };

    /** A pulse's edge at block sample i: starts the TTL and the lockout */
    void startPulse (int i, int edgeOffset, std::vector<Event>& events);
};

#endif
