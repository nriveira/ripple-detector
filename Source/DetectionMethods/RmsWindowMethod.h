#ifndef __RMS_WINDOW_METHOD_H
#define __RMS_WINDOW_METHOD_H

#include "DetectionMethod.h"

/**
    The original Ripple Detector algorithm.

    The block is split into consecutive windows of 'rmsSamples' samples and the RMS of
    each window is compared with (baseline mean + onsetSds * baseline SD). Once the RMS
    has been above threshold for more than 'minDurationMs', a one-window TTL pulse is
    emitted and the refractory period starts.

    Uses: onsetSds, minDurationMs, refractoryMs, rmsSamples, adaptiveBaseline, adaptTauSeconds.
*/
class RmsWindowMethod : public DetectionMethod
{
public:
    std::string getName() const override { return "RMS"; }

    void reset() override;
    void calibrate (const float* data, int numSamples, float* featureOut) override;
    void process (const float* data, int numSamples, std::vector<DetectionEvent>& events, float* featureOut) override;

protected:
    void paramsChanged() override;

private:
    static double windowRms (const float* data, int start, int end);

    int64_t minDurationSamples { 0 };
    int64_t samplesAboveThreshold { 0 };
    bool durationReached { false };
};

#endif
