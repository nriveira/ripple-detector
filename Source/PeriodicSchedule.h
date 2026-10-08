#ifndef __RIPPLE_PERIODIC_SCHEDULE_H
#define __RIPPLE_PERIODIC_SCHEDULE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/**
    The periodic test mode's timer: count onsets, intervalS apart, the first one at
    start() (the START button), or one interval after reset(). The onsets are handed to the pulse detector as external
    onsets, so each one gets the same response as a detected pulse (Ripple Output
    TTL, laser trigger, stimulation latency) while the continuous signal is ignored.

    Free of JUCE so Tests/ can drive it. Not thread-safe: called from the audio thread.
*/
class PeriodicSchedule
{
public:
    /** Sets the count and interval; a shorter interval applies from the next onset on */
    void configure (float sampleRate, int onsetCount, double intervalS)
    {
        count = std::max (0, onsetCount);
        interval = std::max ((int64_t) 1, (int64_t) std::llround (std::max (1.0f, sampleRate) * std::max (0.0, intervalS)));
        if (next > sampleCount)
            next = std::min (next, sampleCount + interval);
    }

    /** Restarts the sequence: no onsets sent, the first one interval from now */
    void reset()
    {
        sampleCount = 0;
        sent = 0;
        next = interval;
    }

    /** Restarts the sequence with its first onset at the next block's first sample */
    void start()
    {
        reset();
        next = 0;
    }

    /** Appends the block-relative samples of this block's onsets to 'out' */
    void onsets (int numSamples, std::vector<int>& out)
    {
        while (sent < (uint32_t) count && next < sampleCount + numSamples)
        {
            out.push_back ((int) (next - sampleCount));
            sent++;
            next += interval;
        }
        sampleCount += numSamples;
    }

    uint32_t getSent() const { return sent; }
    int getCount() const { return count; }
    bool isDone() const { return sent >= (uint32_t) count; }

    /** Samples between onsets */
    int64_t getIntervalSamples() const { return interval; }

private:
    int count { 10 };
    int64_t interval { 60000 };
    int64_t sampleCount { 0 };
    int64_t next { 60000 };
    uint32_t sent { 0 };
};

#endif
