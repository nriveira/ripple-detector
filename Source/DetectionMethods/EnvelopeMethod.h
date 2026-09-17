#ifndef __ENVELOPE_METHOD_H
#define __ENVELOPE_METHOD_H

#include "SampleFeatureMethod.h"

/**
    Rectified, moving-average smoothed amplitude envelope of the band-passed signal
    with dual-threshold (hysteresis) detection.

    This is the causal, real-time analogue of the classic offline approach (band-pass,
    envelope, z-score, onset/offset thresholds, minimum duration).
*/
class EnvelopeMethod : public SampleFeatureMethod
{
public:
    std::string getName() const override { return "Envelope"; }

protected:
    double computeFeature (float sample) override
    {
        return std::fabs ((double) sample);
    }
};

#endif
