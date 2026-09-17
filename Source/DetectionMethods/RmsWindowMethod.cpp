#include "RmsWindowMethod.h"

void RmsWindowMethod::reset()
{
    DetectionMethod::reset();
    samplesAboveThreshold = 0;
    durationReached = false;
}

void RmsWindowMethod::paramsChanged()
{
    minDurationSamples = msToSamples (params.minDurationMs);
}

double RmsWindowMethod::windowRms (const float* data, int start, int end)
{
    double sum = 0.0;
    for (int i = start; i < end; i++)
        sum += (double) data[i] * (double) data[i];

    return std::sqrt (sum / (double) (end - start));
}

void RmsWindowMethod::calibrate (const float* data, int numSamples)
{
    const int window = std::max (1, std::min (params.rmsSamples, numSamples));

    for (int start = 0; start < numSamples; start += window)
    {
        const int end = std::min (start + window, numSamples);
        baseline.accumulate (windowRms (data, start, end));
    }

    samplesProcessed += numSamples;
}

void RmsWindowMethod::process (const float* data, int numSamples, std::vector<DetectionEvent>& events)
{
    const int window = std::max (1, std::min (params.rmsSamples, numSamples));
    const double alpha = params.adaptiveBaseline && params.adaptTauSeconds > 0.0
                             ? (double) window / (params.adaptTauSeconds * params.sampleRate)
                             : 0.0;

    for (int start = 0; start < numSamples; start += window)
    {
        const int end = std::min (start + window, numSamples);
        const int64_t absoluteSample = samplesProcessed + start;
        const double rms = windowRms (data, start, end);

        // The pulse emitted for the previous window ends here
        if (eventActive)
        {
            events.push_back ({ start, false });
            eventActive = false;
        }

        const double threshold = getOnsetThreshold();

        if (rms > threshold)
        {
            samplesAboveThreshold += (end - start);
        }
        else
        {
            samplesAboveThreshold = 0;
            durationReached = false;

            if (alpha > 0.0)
                baseline.adapt (rms, alpha);
        }

        if (samplesAboveThreshold > minDurationSamples)
            durationReached = true;

        if (durationReached && ! inRefractory (absoluteSample))
        {
            events.push_back ({ start, true });
            eventActive = true;
            startRefractory (absoluteSample);
        }
    }

    samplesProcessed += numSamples;
}
