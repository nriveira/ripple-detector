#ifndef __RIPPLE_FEATURE_FIFO_H
#define __RIPPLE_FEATURE_FIFO_H

#include <ProcessorHeaders.h>

#include <array>
#include <cstdint>
#include <vector>

/**
    Single-producer / single-consumer queue that carries the viewer's traces,
    plus per-sample status flags, from the audio thread to the viewer.
    Samples that arrive while the queue is full are dropped.
*/
class FeatureFifo
{
public:
    enum Feature
    {
        SIGNAL_Z = 0, // ripple channel feature, in its baseline SDs
        NOISE_Z, // noise channel feature, in its own baseline SDs (0 without a noise channel)
        RAW, // ripple channel as recorded
        NOISE_RAW, // noise channel as recorded (0 without a noise channel)
        NUM_FEATURES
    };

    enum Flags : uint8_t
    {
        TTL_HIGH = 1 << 0, // ripple output line is high
        CALIBRATING = 1 << 1, // baseline still being estimated
        BLOCKED = 1 << 2, // detection blocked by movement
        NOISE = 1 << 3, // noise channel above threshold
        VETOED = 1 << 4, // a ripple onset suppressed by the noise channel
        TTL_ONSET = 1 << 5 // test modes: the sample at which the output TTL went high
    };

    FeatureFifo() : fifo (1) {}

    /** Allocates room for 'seconds' of data at the given sample rate */
    void setCapacity (float sampleRate, float seconds)
    {
        const int capacity = std::max (1024, (int) (sampleRate * seconds));
        fifo.setTotalSize (capacity);
        for (auto& v : values)
            v.assign ((size_t) capacity, 0.0f);
        flags.assign ((size_t) capacity, 0);
    }

    /** Called from the audio thread */
    void write (const std::array<const float*, NUM_FEATURES>& src, const uint8_t* srcFlags, int numSamples)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        for (int f = 0; f < NUM_FEATURES; f++)
        {
            if (size1 > 0)
                std::copy (src[f], src[f] + size1, values[f].begin() + start1);
            if (size2 > 0)
                std::copy (src[f] + size1, src[f] + size1 + size2, values[f].begin() + start2);
        }

        if (size1 > 0)
            std::copy (srcFlags, srcFlags + size1, flags.begin() + start1);
        if (size2 > 0)
            std::copy (srcFlags + size1, srcFlags + size1 + size2, flags.begin() + start2);

        fifo.finishedWrite (size1 + size2);
    }

    /** Called from the message thread. The callback receives contiguous regions. */
    template <typename Callback>
    void read (Callback&& callback)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

        if (size1 > 0)
            callback (start1, size1);
        if (size2 > 0)
            callback (start2, size2);

        fifo.finishedRead (size1 + size2);
    }

    const float* getValues (int feature, int index) const { return values[(size_t) feature].data() + index; }
    const uint8_t* getFlags (int index) const { return flags.data() + index; }

private:
    AbstractFifo fifo;
    std::array<std::vector<float>, NUM_FEATURES> values;
    std::vector<uint8_t> flags;
};

/**
    Single-producer / single-consumer queue for the viewer's movement panel: up to MAX_TRACES
    movement channels (e.g. the headstage IMU's axes) at their own stream's rate, decimated to at
    most MAX_RATE. Samples that arrive while the queue is full are dropped.
*/
class MovementFifo
{
public:
    static constexpr int MAX_TRACES = 12;
    static constexpr float MAX_RATE = 1000.0f; // Hz

    MovementFifo() : fifo (1) {}

    void setCapacity (int samples)
    {
        const int capacity = std::max (256, samples);
        fifo.setTotalSize (capacity);
        for (auto& v : values)
            v.assign ((size_t) capacity, 0.0f);
    }

    /** Called from the audio thread; traces beyond the queue's MAX_TRACES are not shown */
    void write (const float* const* src, int numTraces, int numSamples)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        for (int t = 0; t < std::min (numTraces, MAX_TRACES); t++)
        {
            if (size1 > 0)
                std::copy (src[t], src[t] + size1, values[(size_t) t].begin() + start1);
            if (size2 > 0)
                std::copy (src[t] + size1, src[t] + size1 + size2, values[(size_t) t].begin() + start2);
        }

        fifo.finishedWrite (size1 + size2);
    }

    /** Called from the message thread. The callback receives contiguous regions. */
    template <typename Callback>
    void read (Callback&& callback)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

        if (size1 > 0)
            callback (start1, size1);
        if (size2 > 0)
            callback (start2, size2);

        fifo.finishedRead (size1 + size2);
    }

    const float* getValues (int trace, int index) const { return values[(size_t) trace].data() + index; }

private:
    AbstractFifo fifo;
    std::array<std::vector<float>, MAX_TRACES> values;
};

#endif
