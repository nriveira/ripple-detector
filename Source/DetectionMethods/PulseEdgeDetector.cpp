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
    noiseAlpha = 1.0 - std::exp (-1.0 / (fs * NOISE_TAU_MS / 1000.0));
    warmupSamples = std::max ((int64_t) 1, (int64_t) std::ceil (fs * NOISE_WARMUP_MS / 1000.0));
    excursionHoldSamples = (int64_t) std::ceil (fs * EXCURSION_HOLD_MS / 1000.0);

    // Min Width in samples, and how many of them may dip below the threshold
    confirmSamples = std::max (MIN_CONFIRM_SAMPLES, (int) std::lround (fs * std::max (0.0, params.minWidthMs) / 1000.0));
    allowedBelow = confirmSamples - (int) std::ceil (CONFIRM_FRACTION * confirmSamples);

    // The TTL pulse ends inside the lockout, so the output always returns low before the next onset
    ttlSamples = std::min (ttlSamples, std::max ((int64_t) 1, lockoutSamples));
}

void PulseEdgeDetector::reset()
{
    started = false;
    baseline = 0.0;
    noiseMad = 0.0;
    noiseSamples = 0;
    noiseReady = false;
    armed = true;
    excursion = 0;
    confirming = false;
    candidateTotal = 0;
    candidateBelow = 0;
    lockoutLeft = 0;
    ttlLeft = 0;
    detections = 0;
    ignoredExternal = 0;
    rejected = 0;
    sampleCount = 0;
}

double PulseEdgeDetector::getThreshold() const
{
    if (! noiseReady)
        return 0.0;

    return std::max (std::max (0.0, params.minStepUv), std::max (0.0, params.thresholdSds) * getNoiseSd());
}

void PulseEdgeDetector::track (double x, double deviation, bool updateNoise)
{
    baseline += alpha * (x - baseline);

    if (! updateNoise)
        return;

    // A plain mean until the warm-up is over, then a running mean
    noiseSamples++;
    const double weight = noiseReady ? noiseAlpha : 1.0 / (double) noiseSamples;
    noiseMad += weight * (std::fabs (deviation) - noiseMad);
    if (! noiseReady && noiseSamples >= warmupSamples)
        noiseReady = true;
}

void PulseEdgeDetector::startPulse (int i, int edgeOffset, std::vector<Event>& events)
{
    events.push_back ({ i, Event::Onset, edgeOffset });
    detections++;

    confirming = false;
    armed = false;
    ttlLeft = ttlSamples;
    lockoutLeft = lockoutSamples;
}

void PulseEdgeDetector::process (const float* data, int numSamples, std::vector<Event>& events,
                                 float* featureOut, uint8_t* lockoutOut, const std::vector<int>* externalOnsets)
{
    std::size_t nextExternal = 0;

    for (int i = 0; i < numSamples; i++, sampleCount++)
    {
        // An external edge at this sample (any not yet consumed count here too)
        bool externalHere = false;
        if (externalOnsets != nullptr)
            while (nextExternal < externalOnsets->size() && (*externalOnsets)[nextExternal] <= i)
            {
                externalHere = true;
                nextExternal++;
            }

        const double x = (double) data[i];

        if (! started)
        {
            baseline = x;
            started = true;
        }

        const double deviation = x - baseline;

        if (featureOut != nullptr)
            featureOut[i] = (float) deviation;
        if (lockoutOut != nullptr)
            lockoutOut[i] = (lockoutLeft > 0 || ! armed) ? 1 : 0;

        // The TTL pulse started by the last edge
        if (ttlLeft > 0 && --ttlLeft == 0)
            events.push_back ({ i, Event::TtlOff, 0 });

        // Locked out: the baseline and the noise estimate are frozen and no new pulse is detected
        if (lockoutLeft > 0)
        {
            lockoutLeft--;
            if (externalHere)
                ignoredExternal++;
            continue;
        }

        // External onsets: the channel's own crossings are not used, only its baseline is kept
        if (externalOnsets != nullptr)
        {
            armed = true;
            confirming = false;
            if (externalHere)
                startPulse (i, 0, events);
            else
                track (x, deviation, true);
            continue;
        }

        const double threshold = getThreshold();

        if (! armed)
        {
            // Wait for the signal to settle back before looking for the next edge
            track (x, deviation, false);
            if (deviation < REARM_FRACTION * threshold)
                armed = true;
            continue;
        }

        if (confirming)
        {
            // The baseline stays put so it does not chase the edge
            candidateTotal++;
            if (deviation <= threshold)
                candidateBelow++;

            if (candidateBelow > allowedBelow)
            {
                // A spike or a burst of noise, not a step
                confirming = false;
                rejected++;
                track (x, deviation, false);
            }
            else if (candidateTotal >= confirmSamples)
            {
                startPulse (i, (int) (sampleCount - candidateStart), events);
            }
            continue;
        }

        if (threshold > 0.0 && deviation > threshold)
        {
            confirming = true;
            candidateStart = sampleCount;
            candidateTotal = 1;
            candidateBelow = 0;
            excursion = 0;
            continue;
        }

        // A dip (an inverted pulse): hold the baseline, so the way back up is not a rising edge
        if (threshold > 0.0 && deviation < -threshold && excursion < excursionHoldSamples)
        {
            excursion++;
            continue;
        }
        if (deviation >= -threshold)
            excursion = 0;

        track (x, deviation, true);
    }
}
