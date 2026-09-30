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
    settleSamples = std::max (1, (int) std::ceil (fs * SETTLE_MS / 1000.0));

    const std::size_t diffSamples = (std::size_t) std::max (1, (int) std::ceil (fs * RESPONSE_DIFF_MS / 1000.0));
    if (history.size() != diffSamples)
    {
        history.assign (diffSamples, 0.0);
        historyIndex = 0;
    }

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
    responseState = ResponseState::Idle;
    settled = 0;
    responseAbove = 0;
    sampleCount = 0;
    pulseEdge = 0;
    historyIndex = 0;
    started = false; // the history is refilled from the first sample
}

void PulseEdgeDetector::process (const float* data, int numSamples, std::vector<Event>& events,
                                 float* featureOut, uint8_t* lockoutOut)
{
    const double threshold = std::max (0.0, params.thresholdUv);
    const double responseThreshold = std::max (0.0, params.responseThresholdUv);

    for (int i = 0; i < numSamples; i++, sampleCount++)
    {
        const double x = (double) data[i];

        if (! started)
        {
            baseline = x;
            std::fill (history.begin(), history.end(), x);
            started = true;
        }

        const double level = std::fabs (x - baseline);

        // Change over the last RESPONSE_DIFF_MS, for the response
        const double previous = history[historyIndex];
        history[historyIndex] = x;
        historyIndex = (historyIndex + 1) % history.size();
        const double change = std::fabs (x - previous);

        if (featureOut != nullptr)
            featureOut[i] = (float) level;
        if (lockoutOut != nullptr)
            lockoutOut[i] = (lockoutLeft > 0 || ! armed) ? 1 : 0;

        // The TTL pulse started by the last edge
        if (ttlLeft > 0 && --ttlLeft == 0)
            events.push_back ({ i, Event::TtlOff, 0, 0 });

        // Locked out: the baseline is frozen and no new pulse is detected, but the
        // stimulation artefact that follows the pulse is looked for
        if (lockoutLeft > 0)
        {
            lockoutLeft--;

            if (responseState == ResponseState::PulseEnding)
            {
                // Settled once the comparison window below no longer contains the pulse or its falling edge
                settled = level < responseThreshold ? settled + 1 : 0;
                if (settled >= settleSamples + (int) history.size())
                    responseState = ResponseState::Waiting;
            }
            else if (responseState == ResponseState::Waiting)
            {
                if (change > responseThreshold)
                {
                    if (++responseAbove >= CONFIRM_SAMPLES)
                    {
                        const int64_t edge = sampleCount - (responseAbove - 1);
                        events.push_back ({ i, Event::Response, responseAbove - 1, edge - pulseEdge });
                        responseState = ResponseState::Idle;
                    }
                }
                else
                {
                    responseAbove = 0;
                }
            }

            if (lockoutLeft == 0)
                responseState = ResponseState::Idle; // no response within the lockout
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
                events.push_back ({ i, Event::Onset, above - 1, 0 });
                detections++;
                pulseEdge = sampleCount - (above - 1);

                // Look for the stimulation's artefact once the test pulse has passed
                responseState = responseThreshold > 0.0 && lockoutSamples > 0 ? ResponseState::PulseEnding : ResponseState::Idle;
                settled = 0;
                responseAbove = 0;

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
