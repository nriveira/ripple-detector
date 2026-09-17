#include "SampleFeatureMethod.h"

void SampleFeatureMethod::reset()
{
    DetectionMethod::reset();
    samplesAboveOnset = 0;
    eventStartSample = 0;
    resetFeature();
    resizeSmoothingBuffer();
}

void SampleFeatureMethod::paramsChanged()
{
    minDurationSamples = msToSamples (params.minDurationMs);
    maxDurationSamples = params.maxDurationMs > 0 ? msToSamples (params.maxDurationMs) : 0;

    const int newLength = (int) std::max ((int64_t) 1, msToSamples (params.smoothingMs));
    if (newLength != smoothingLength)
    {
        smoothingLength = newLength;
        resizeSmoothingBuffer();
    }
}

void SampleFeatureMethod::resizeSmoothingBuffer()
{
    smoothingBuffer.assign ((size_t) smoothingLength, 0.0);
    smoothingIndex = 0;
    smoothingFill = 0;
    smoothingSum = 0.0;
}

double SampleFeatureMethod::smooth (double value)
{
    smoothingSum -= smoothingBuffer[(size_t) smoothingIndex];
    smoothingBuffer[(size_t) smoothingIndex] = value;
    smoothingSum += value;

    smoothingIndex = (smoothingIndex + 1) % smoothingLength;
    if (smoothingFill < smoothingLength)
        smoothingFill++;

    return smoothingSum / (double) smoothingFill;
}

void SampleFeatureMethod::calibrate (const float* data, int numSamples)
{
    for (int i = 0; i < numSamples; i++)
        baseline.accumulate (smooth (computeFeature (data[i])));

    samplesProcessed += numSamples;
}

void SampleFeatureMethod::process (const float* data, int numSamples, std::vector<DetectionEvent>& events)
{
    const double alpha = params.adaptiveBaseline && params.adaptTauSeconds > 0.0
                             ? 1.0 / (params.adaptTauSeconds * params.sampleRate)
                             : 0.0;

    for (int i = 0; i < numSamples; i++)
    {
        const int64_t absoluteSample = samplesProcessed + i;
        const double feature = smooth (computeFeature (data[i]));

        if (! eventActive)
        {
            if (feature > getOnsetThreshold())
            {
                samplesAboveOnset++;
            }
            else
            {
                samplesAboveOnset = 0;

                if (alpha > 0.0)
                    baseline.adapt (feature, alpha);
            }

            if (samplesAboveOnset >= minDurationSamples && ! inRefractory (absoluteSample))
            {
                events.push_back ({ i, true });
                eventActive = true;
                eventStartSample = absoluteSample - samplesAboveOnset + 1;
                samplesAboveOnset = 0;
            }
        }
        else
        {
            const bool belowOffset = feature < getOffsetThreshold();
            const bool tooLong = maxDurationSamples > 0 && (absoluteSample - eventStartSample) >= maxDurationSamples;

            if (belowOffset || tooLong)
            {
                events.push_back ({ i, false });
                eventActive = false;
                startRefractory (absoluteSample);
            }
        }
    }

    samplesProcessed += numSamples;
}
