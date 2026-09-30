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

    Response: when the stimulation is fed back into the same bath, its artefact
    appears on the channel a few milliseconds after the pulse. With a response
    threshold set, the detector first waits for the test pulse to pass: its
    distance from the pre-pulse baseline must stay below the response threshold
    for SETTLE_MS plus RESPONSE_DIFF_MS, so that neither the pulse nor its falling
    edge is inside the window compared below. For a 1 ms test pulse the earliest
    measurable stimulus is therefore 1.5 ms after the edge. It then looks for an
    abrupt change: the first sample
    that differs from the one RESPONSE_DIFF_MS earlier by more than the response
    threshold (CONFIRM_SAMPLES in a row) is the stimulus, and its time relative to
    the pulse's edge is reported. An isolator's artefact jumps within a fraction
    of a millisecond, while what is left of the test pulse drifts slowly, so the
    tail of a long or AC-coupled pulse is not mistaken for a stimulus.

    One response per pulse, within the lockout. A stimulus that arrives while the
    test pulse is still above the response threshold cannot be separated from it
    and is not reported, so the test pulses should be short (1 ms) and the
    response threshold above what is left of the test pulse.

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
        double responseThresholdUv { 0.0 }; // stimulation artefact threshold; 0 = no response measurement
    };

    /** Something that happened in the current block */
    struct Event
    {
        enum Type
        {
            Onset, // a pulse edge: the output TTL goes high
            TtlOff, // the output TTL goes low
            Response // the stimulation artefact after a pulse
        };

        int sampleIndex; // the sample that confirmed it (Onset, Response) or the TTL's end (TtlOff)
        Type type;
        int edgeOffset; // Onset, Response: samples from the first threshold crossing back to sampleIndex (>= 0)
        int64_t latencySamples; // Response: from the pulse's edge to the response's edge
    };

    static constexpr int CONFIRM_SAMPLES = 2; // consecutive samples above threshold
    static constexpr double SETTLE_MS = 0.25; // the test pulse must stay below the response threshold this long
    static constexpr double RESPONSE_DIFF_MS = 0.25; // a response is a change of more than its threshold over this long
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
    */
    void process (const float* data, int numSamples, std::vector<Event>& events,
                  float* featureOut = nullptr, uint8_t* lockoutOut = nullptr);

    bool isLockedOut() const { return lockoutLeft > 0; }
    bool isWaitingForResponse() const { return responseState != ResponseState::Idle; }
    bool isTtlHigh() const { return ttlLeft > 0; }
    uint32_t getDetections() const { return detections; }

private:
    Params params;

    int64_t lockoutSamples { 30000 };
    int64_t ttlSamples { 300 };
    int settleSamples { 8 };
    std::vector<double> history; // the last RESPONSE_DIFF_MS of samples, for the response's change
    std::size_t historyIndex { 0 };
    double alpha { 0.0 }; // baseline update weight per sample

    enum class ResponseState
    {
        Idle, // not measuring (no pulse, response found, or measurement off)
        PulseEnding, // waiting for the test pulse to drop below the response threshold
        Waiting // looking for the stimulation artefact
    };
    ResponseState responseState { ResponseState::Idle };
    int settled { 0 }; // consecutive samples below the response threshold
    int responseAbove { 0 }; // consecutive samples above it
    int64_t sampleCount { 0 }; // samples since reset(), for latencies across blocks
    int64_t pulseEdge { 0 }; // sampleCount of the last pulse's edge

    bool started { false };
    double baseline { 0.0 };
    bool armed { true };
    int above { 0 }; // consecutive samples above threshold
    int64_t lockoutLeft { 0 };
    int64_t ttlLeft { 0 };
    uint32_t detections { 0 };
};

#endif
