#include "PulseEdgeDetector.h"

#include <algorithm>
#include <cmath>

void PulseEdgeDetector::setParams (const Params& p)
{
    params = p;

    const double fs = std::max (1.0f, params.sampleRate);
    lockoutSamples = (int64_t) std::ceil (fs * std::max (0.0, params.lockoutMs) / 1000.0);
    ttlSamples = std::max ((int64_t) 1, (int64_t) std::ceil (fs * std::max (0.0, params.ttlMs) / 1000.0));
    alpha = 1.0 - std::exp (-1.0 / (fs * BASELINE_TAU_MS / 1000.0));

    // The TTL pulse ends inside the lockout, so the output always returns low before the next onset
    ttlSamples = std::min (ttlSamples, std::max ((int64_t) 1, lockoutSamples));
}

void PulseEdgeDetector::reset()
{
    started = false;
    baseline = 0.0;
    armed = true;
    above = 0;
    lockoutLeft = 0;
    ttlLeft = 0;
    detections = 0;
}

void PulseEdgeDetector::process (const float* data, int numSamples, std::vector<Event>& events,
                                 float* featureOut, uint8_t* lockoutOut)
{
    const double threshold = std::max (0.0, params.thresholdUv);

    for (int i = 0; i < numSamples; i++)
    {
        const double x = (double) data[i];

        if (! started)
        {
            baseline = x;
            started = true;
        }

        const double level = std::fabs (x - baseline);

        if (featureOut != nullptr)
            featureOut[i] = (float) level;
        if (lockoutOut != nullptr)
            lockoutOut[i] = (lockoutLeft > 0 || ! armed) ? 1 : 0;

        // The TTL pulse started by the last edge
        if (ttlLeft > 0 && --ttlLeft == 0)
            events.push_back ({ i, false, 0 });

        // Locked out: the baseline is frozen and nothing is detected
        if (lockoutLeft > 0)
        {
            lockoutLeft--;
            continue;
        }

        if (! armed)
        {
            // Wait for the signal to settle back before looking for the next edge
            baseline += alpha * (x - baseline);
            if (level < REARM_FRACTION * threshold)
                armed = true;
            continue;
        }

        if (threshold > 0.0 && level > threshold)
        {
            if (++above >= CONFIRM_SAMPLES)
            {
                events.push_back ({ i, true, above - 1 });
                detections++;

                above = 0;
                armed = false;
                ttlLeft = ttlSamples;
                lockoutLeft = lockoutSamples;
            }
            // While confirming, the baseline stays put so it does not chase the edge
        }
        else
        {
            above = 0;
            baseline += alpha * (x - baseline);
        }
    }
}
