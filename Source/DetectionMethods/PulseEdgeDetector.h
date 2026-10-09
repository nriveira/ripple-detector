#ifndef __PULSE_EDGE_DETECTOR_H
#define __PULSE_EDGE_DETECTOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

/**
    Debug detector: finds the rising edge of a square pulse on one channel, for
    measuring the closed-loop latency with a known input (e.g. test pulses played
    into saline) instead of ripples.

    Each sample is compared with a slowly tracking baseline. The threshold adapts
    to the channel's noise: it is thresholdSds times a running estimate of the
    noise SD, but never below minStepUv. So the same settings work on a quiet
    recording and on a noisy saline signal, and pulses of different amplitudes are
    found as long as they stand clear of the noise.

    Only rising edges count: the signal must rise above baseline + threshold and
    stay there for minWidthMs (at least CONFIRM_FRACTION of those samples, so
    noise riding on the pulse does not break it up). Spikes and bursts shorter
    than that, falling edges and the undershoot of an AC-coupled source are
    ignored. A dip below baseline - threshold (an inverted pulse) holds the
    baseline for up to EXCURSION_HOLD_MS, so its return is not taken for a rise;
    a longer shift is followed. The edge time is the first sample above the
    threshold; the decision is the sample that confirmed it.

    After an edge, detection is locked out for lockoutMs. That hides the pulse's
    end and any artefact of the stimulation that follows, so each input pulse is
    paired with exactly one response. Detection re-arms only once the signal is
    back near its baseline. The baseline and the noise estimate are frozen while
    a pulse is being confirmed and during the lockout, so pulses do not inflate
    the noise. Nothing is detected until NOISE_WARMUP_MS of signal has given a
    first noise estimate.

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
        double thresholdSds { 8.0 }; // the rise must exceed this many noise SDs ...
        double minStepUv { 100.0 }; // ... and at least this much, in the channel's units (both 0: detection off)
        double minWidthMs { 0.3 }; // and stay above the threshold this long
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

    static constexpr double CONFIRM_FRACTION = 0.75; // share of the confirmation samples that must be above threshold
    static constexpr int MIN_CONFIRM_SAMPLES = 2;
    static constexpr double BASELINE_TAU_MS = 20.0; // baseline tracking time constant
    static constexpr double NOISE_TAU_MS = 1000.0; // noise estimate time constant
    static constexpr double NOISE_WARMUP_MS = 200.0; // signal needed before the first detection
    static constexpr double REARM_FRACTION = 0.5; // after the lockout, re-arm below this share of the threshold
    static constexpr double EXCURSION_HOLD_MS = 100.0; // a dip holds the baseline this long at most

    void setParams (const Params& p);
    const Params& getParams() const { return params; }

    /** Clears all state; the next sample starts the baseline and the noise estimate */
    void reset();

    /**
        Runs one block, appending TTL edges to 'events'. Optional outputs per sample:
        featureOut receives x - baseline (signed); lockoutOut is 1 while detection is
        locked out or waiting to re-arm, 0 while armed.

        externalOnsets (optional, sorted block-relative sample indices): the onsets
        come from elsewhere and the channel's crossings are not used; see above.
    */
    void process (const float* data, int numSamples, std::vector<Event>& events,
                  float* featureOut = nullptr, uint8_t* lockoutOut = nullptr,
                  const std::vector<int>* externalOnsets = nullptr);

    /** The noise SD estimate and the threshold it gives now (0 before the warm-up is over) */
    double getNoiseSd() const { return noiseReady ? noiseMad * MAD_TO_SD : 0.0; }
    double getThreshold() const;

    bool isLockedOut() const { return lockoutLeft > 0; }
    bool isTtlHigh() const { return ttlLeft > 0; }
    uint32_t getDetections() const { return detections; }
    uint32_t getIgnoredExternal() const { return ignoredExternal; } // external onsets that fell in a lockout
    uint32_t getRejected() const { return rejected; } // crossings that did not stay up for Min Width

private:
    static constexpr double MAD_TO_SD = 1.2533141373155; // mean absolute deviation -> SD, for Gaussian noise

    Params params;

    int64_t lockoutSamples { 30000 };
    int64_t ttlSamples { 300 };
    int confirmSamples { 9 };
    int allowedBelow { 2 }; // confirmation samples that may dip below the threshold
    double alpha { 0.0 }; // baseline update weight per sample
    double noiseAlpha { 0.0 }; // noise estimate update weight per sample
    int64_t warmupSamples { 6000 };
    int64_t excursionHoldSamples { 3000 };
    int64_t excursion { 0 }; // samples the signal has been below baseline - threshold

    int64_t sampleCount { 0 }; // samples since reset(), for latencies across blocks

    bool started { false };
    double baseline { 0.0 };
    double noiseMad { 0.0 }; // running mean of |x - baseline| while idle
    int64_t noiseSamples { 0 };
    bool noiseReady { false };
    bool armed { true };

    // A rising crossing being confirmed
    bool confirming { false };
    int64_t candidateStart { 0 }; // sampleCount of the first sample above threshold
    int candidateTotal { 0 };
    int candidateBelow { 0 };

    int64_t lockoutLeft { 0 };
    int64_t ttlLeft { 0 };
    uint32_t detections { 0 };
    uint32_t ignoredExternal { 0 };
    uint32_t rejected { 0 };

    /** A pulse's edge at block sample i: starts the TTL and the lockout */
    void startPulse (int i, int edgeOffset, std::vector<Event>& events);

    /** Tracks the baseline, and the noise estimate while nothing is going on */
    void track (double x, double deviation, bool updateNoise);
};

#endif
