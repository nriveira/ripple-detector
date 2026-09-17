#ifndef __RIPPLE_DETECTION_METHOD_H
#define __RIPPLE_DETECTION_METHOD_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// This layer is deliberately free of JUCE / Open Ephys dependencies so that the
// algorithms can be unit-tested outside the GUI (see Tests/).

/**
    Parameters shared by all detection methods. Not every method uses every field;
    each method documents which ones it reads.
*/
struct DetectionParams
{
    float sampleRate { 30000.0f };

    double onsetSds { 5.0 }; // "ripple_std": SDs above baseline mean that start an event
    double offsetSds { 2.0 }; // "ripple_std_off": SDs above baseline mean that end an event (hysteresis methods)
    int minDurationMs { 10 }; // "time_thresh": minimum time above the onset threshold before an event is emitted
    int maxDurationMs { 0 }; // "max_dur": force the event to end after this many ms (0 = disabled)
    int refractoryMs { 140 }; // "refr_time": no new events for this long after an event
    int rmsSamples { 128 }; // "rms_samples": window length for RMS-based methods
    double smoothingMs { 5.0 }; // "smooth_ms": moving-average window for per-sample feature methods

    bool adaptiveBaseline { false }; // "baseline": keep updating the baseline after calibration
    double adaptTauSeconds { 60.0 }; // "adapt_tau": time constant of the adaptive baseline
};

/** A TTL edge produced by a detection method, relative to the start of the current block. */
struct DetectionEvent
{
    int sampleIndex;
    bool state; // true = event onset (TTL high), false = event offset (TTL low)
};

/**
    Running mean / standard deviation of a scalar feature.

    During calibration values are accumulated with Welford's algorithm. Afterwards the
    statistics can optionally be tracked with an exponential moving average so the
    threshold follows slow drifts in the recording.
*/
class BaselineStats
{
public:
    void clear()
    {
        count = 0;
        mean = 0.0;
        m2 = 0.0;
        variance = 0.0;
    }

    /** Adds one calibration value */
    void accumulate (double value)
    {
        count++;
        const double delta = value - mean;
        mean += delta / (double) count;
        m2 += delta * (value - mean);
    }

    /** Finalises the calibration statistics (sample variance, n - 1) */
    void finish()
    {
        variance = count > 1 ? m2 / (double) (count - 1) : 0.0;
    }

    /** Moves the statistics towards a new observation with weight alpha */
    void adapt (double value, double alpha)
    {
        const double delta = value - mean;
        mean += alpha * delta;
        variance += alpha * (delta * delta - variance);
        if (variance < 0.0)
            variance = 0.0;
    }

    double getMean() const { return mean; }
    double getStd() const { return std::sqrt (variance); }
    int64_t getCount() const { return count; }

private:
    int64_t count { 0 };
    double mean { 0.0 };
    double m2 { 0.0 };
    double variance { 0.0 };
};

/**
    Base class for ripple detection algorithms.

    Lifecycle:
      1. setParams()          -- may be called at any time, including during processing
      2. reset()              -- clears all state and starts a calibration period
      3. calibrate()          -- called with each block until the host decides calibration is over
      4. finishCalibration()  -- computes baseline statistics
      5. process()            -- called with each block; appends TTL edges to the output vector

    Methods only see the (already band-pass filtered) ripple channel. Movement gating,
    TTL emission and stream bookkeeping are handled by the RippleDetector processor.
*/
class DetectionMethod
{
public:
    virtual ~DetectionMethod() = default;

    /** Human-readable name, used for logging */
    virtual std::string getName() const = 0;

    /** Updates parameters; safe to call between blocks */
    void setParams (const DetectionParams& newParams)
    {
        params = newParams;
        paramsChanged();
    }

    const DetectionParams& getParams() const { return params; }

    /** Clears all state and begins calibration */
    virtual void reset()
    {
        baseline.clear();
        samplesProcessed = 0;
        eventActive = false;
        refractoryUntil = -1;
        paramsChanged();
    }

    /** Accumulates baseline statistics from one block of data.
        If featureOut is not null, the per-sample feature value is written to it. */
    virtual void calibrate (const float* data, int numSamples, float* featureOut = nullptr) = 0;

    /** Finalises baseline statistics after the calibration period */
    virtual void finishCalibration()
    {
        baseline.finish();
    }

    /** Runs detection on one block, appending TTL edges to 'events'.
        If featureOut is not null, the per-sample feature value is written to it. */
    virtual void process (const float* data, int numSamples, std::vector<DetectionEvent>& events, float* featureOut = nullptr) = 0;

    /** True while an event is ongoing (TTL high) */
    bool isEventActive() const { return eventActive; }

    double getBaselineMean() const { return baseline.getMean(); }
    double getBaselineStd() const { return baseline.getStd(); }
    double getOnsetThreshold() const { return baseline.getMean() + params.onsetSds * baseline.getStd(); }
    double getOffsetThreshold() const
    {
        // The offset threshold can never be above the onset threshold
        return baseline.getMean() + std::min (params.offsetSds, params.onsetSds) * baseline.getStd();
    }

    /** Sample count for a duration in milliseconds at the current sample rate */
    int64_t msToSamples (double ms) const
    {
        return (int64_t) std::ceil (params.sampleRate * ms / 1000.0);
    }

protected:
    /** Called whenever parameters change so derived classes can recompute sample counts */
    virtual void paramsChanged() {}

    /** True if a new event may not start at this absolute sample */
    bool inRefractory (int64_t absoluteSample) const
    {
        return absoluteSample < refractoryUntil;
    }

    /** Starts the refractory period at the given absolute sample */
    void startRefractory (int64_t absoluteSample)
    {
        refractoryUntil = absoluteSample + msToSamples (params.refractoryMs);
    }

    DetectionParams params;
    BaselineStats baseline;

    int64_t samplesProcessed { 0 }; // absolute sample counter since reset()
    bool eventActive { false };
    int64_t refractoryUntil { -1 };
};

#endif
