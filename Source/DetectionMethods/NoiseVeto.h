#ifndef __RIPPLE_NOISE_VETO_H
#define __RIPPLE_NOISE_VETO_H

#include "DetectionMethod.h"

/**
    Vetoes ripple onsets that also appear on a noise channel.

    A second detection method runs on the noise channel with the signal
    channel's settings, against the noise channel's own calibrated baseline.
    A signal onset is vetoed when the noise channel is above its onset
    threshold in the same RMS window: something that crosses threshold on
    both channels at once is treated as noise (movement, chewing, electrical
    artefact) rather than a ripple.

    Only the same window counts, so the veto never delays a stimulus. The
    windows line up because both methods receive the same blocks and split
    them into windows the same way.

    The noise side does not also have to satisfy the minimum duration: the
    signal onset already did. With the two channels' backgrounds differing,
    the noise channel often completes its own duration one window after the
    signal does, and requiring it would let those common-mode events through
    (Tests/DetectionMethodTests.cpp, testNoiseVeto). For the same reason the
    noise method runs without a refractory period, so it reports every window
    above threshold, not only the first one.
*/
class NoiseVeto
{
public:
    explicit NoiseVeto (std::unique_ptr<DetectionMethod> m) : method (std::move (m)) {}

    /** Takes the signal channel's settings */
    void setParams (DetectionParams signalParams)
    {
        signalParams.refractoryMs = 0;
        signalParams.minDurationMs = 0;
        method->setParams (signalParams);
    }

    void reset()
    {
        method->reset();
        noiseOnsets.clear();
    }

    void calibrate (const float* noise, int numSamples, float* featureOut = nullptr)
    {
        method->calibrate (noise, numSamples, featureOut);
    }

    void finishCalibration() { method->finishCalibration(); }

    /** Runs detection on the noise channel's block; call before vetoes() for that block */
    void process (const float* noise, int numSamples, float* featureOut = nullptr)
    {
        events.clear();
        noiseOnsets.clear();
        method->process (noise, numSamples, events, featureOut);

        for (const auto& e : events)
            if (e.state)
                noiseOnsets.push_back (e.sampleIndex);
    }

    /** True if the noise channel was above threshold in the window starting at this block sample */
    bool vetoes (int sampleIndex) const
    {
        for (int onset : noiseOnsets)
            if (onset == sampleIndex)
                return true;
        return false;
    }

    /** Block-relative starts of the windows in which the noise channel was above threshold */
    const std::vector<int>& getNoiseWindows() const { return noiseOnsets; }

    const DetectionMethod& getMethod() const { return *method; }

private:
    std::unique_ptr<DetectionMethod> method;
    std::vector<DetectionEvent> events;
    std::vector<int> noiseOnsets;
};

#endif
