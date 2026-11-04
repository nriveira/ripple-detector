#ifndef __RIPPLE_NOISE_VETO_H
#define __RIPPLE_NOISE_VETO_H

#include "DetectionMethod.h"

/**
    Vetoes ripple onsets that also appear on a noise channel.

    A second detection method of the same kind runs on the noise channel with
    the signal channel's settings, against the noise channel's own calibrated
    baseline. A signal onset is vetoed when the noise channel is in an event at
    the onset's sample: something that crosses threshold on both channels at
    once is treated as noise (movement, chewing, electrical artefact) rather
    than a ripple. With RMS that is the same window; with Envelope and TKEO, the
    noise channel's feature has crossed its onset threshold and not yet fallen
    below its offset threshold.

    Only the onset's own sample counts, so the veto never delays a stimulus.

    The noise side does not also have to satisfy the minimum duration: the
    signal onset already did. With the two channels' backgrounds differing,
    the noise channel often completes its own duration one window after the
    signal does, and requiring it would let those common-mode events through
    (Tests/DetectionMethodTests.cpp, testNoiseVeto). For the same reason the
    noise method runs without a refractory period, so with RMS it reports every
    window above threshold, not only the first one.
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
        noisy.clear();
    }

    void calibrate (const float* noise, int numSamples, float* featureOut = nullptr)
    {
        method->calibrate (noise, numSamples, featureOut);
    }

    void finishCalibration() { method->finishCalibration(); }

    /** Runs detection on the noise channel's block; call before vetoes() for that block */
    void process (const float* noise, int numSamples, float* featureOut = nullptr)
    {
        bool active = method->isEventActive(); // an event carried over from the previous block
        events.clear();
        method->process (noise, numSamples, events, featureOut);

        if ((int) noisy.size() < numSamples)
            noisy.resize ((size_t) numSamples);
        blockSamples = numSamples;

        int cursor = 0;
        for (const auto& e : events)
        {
            std::fill (noisy.begin() + cursor, noisy.begin() + std::max (cursor, e.sampleIndex), (uint8_t) active);
            cursor = std::max (cursor, e.sampleIndex);
            active = e.state;
        }
        std::fill (noisy.begin() + cursor, noisy.begin() + numSamples, (uint8_t) active);
    }

    /** True if the noise channel is in an event at this block sample */
    bool vetoes (int sampleIndex) const { return isNoisy (sampleIndex); }

    /** The same, for marking the viewer */
    bool isNoisy (int sampleIndex) const { return sampleIndex >= 0 && sampleIndex < blockSamples && noisy[(size_t) sampleIndex] != 0; }

    const DetectionMethod& getMethod() const { return *method; }

private:
    std::unique_ptr<DetectionMethod> method;
    std::vector<DetectionEvent> events;
    std::vector<uint8_t> noisy; // per sample of the last block: the noise channel is in an event
    int blockSamples { 0 };
};

#endif
