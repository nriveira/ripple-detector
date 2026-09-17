#ifndef __SAMPLE_FEATURE_METHOD_H
#define __SAMPLE_FEATURE_METHOD_H

#include "DetectionMethod.h"

/**
    Base class for methods that compute a per-sample feature (e.g. the rectified signal
    or Teager-Kaiser energy), smooth it with a moving average and apply a dual-threshold
    state machine:

      - An event starts once the smoothed feature has stayed above
        (mean + onsetSds * SD) for 'minDurationMs'.
      - The event ends when the feature drops below (mean + offsetSds * SD), or after
        'maxDurationMs' if that is set.
      - A new event cannot start until 'refractoryMs' after the previous one ended.

    Uses: onsetSds, offsetSds, minDurationMs, maxDurationMs, refractoryMs, smoothingMs,
          adaptiveBaseline, adaptTauSeconds.
*/
class SampleFeatureMethod : public DetectionMethod
{
public:
    void reset() override;
    void calibrate (const float* data, int numSamples) override;
    void process (const float* data, int numSamples, std::vector<DetectionEvent>& events) override;

protected:
    void paramsChanged() override;

    /** Computes the raw (unsmoothed) feature for one sample. Called once per sample, in order. */
    virtual double computeFeature (float sample) = 0;

    /** Clears any per-sample history kept by the feature */
    virtual void resetFeature() {}

private:
    /** Pushes a value through the moving-average filter and returns the smoothed value */
    double smooth (double value);

    void resizeSmoothingBuffer();

    std::vector<double> smoothingBuffer;
    int smoothingLength { 1 };
    int smoothingIndex { 0 };
    int smoothingFill { 0 };
    double smoothingSum { 0.0 };

    int64_t minDurationSamples { 0 };
    int64_t maxDurationSamples { 0 };
    int64_t samplesAboveOnset { 0 };
    int64_t eventStartSample { 0 };
};

#endif
