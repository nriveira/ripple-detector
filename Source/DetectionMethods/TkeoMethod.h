#ifndef __TKEO_METHOD_H
#define __TKEO_METHOD_H

#include "SampleFeatureMethod.h"

/**
    Teager-Kaiser Energy Operator (TKEO) with dual-threshold detection.

        psi[n] = x[n]^2 - x[n-1] * x[n+1]

    The operator is proportional to (amplitude * frequency)^2, so it emphasises
    high-frequency oscillations and suppresses slower components that leak through
    the band-pass filter. The output is delayed by one sample so that x[n+1] is
    available; the smoothed energy is then thresholded like the envelope method.
*/
class TkeoMethod : public SampleFeatureMethod
{
public:
    std::string getName() const override { return "TKEO"; }

protected:
    double computeFeature (float sample) override
    {
        const double x = (double) sample;
        // Energy at the previous sample, using the current sample as x[n+1]
        const double energy = prev1 * prev1 - prev2 * x;

        prev2 = prev1;
        prev1 = x;

        return energy;
    }

    void resetFeature() override
    {
        prev1 = 0.0;
        prev2 = 0.0;
    }

private:
    double prev1 { 0.0 };
    double prev2 { 0.0 };
};

#endif
