#ifndef __RIPPLE_STIM_LATENCY_METER_H
#define __RIPPLE_STIM_LATENCY_METER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

/**
    Measures the stimulation latency from the recording itself: each ripple
    onset (the detector's TTL) is paired with the next rising edge of the
    stimulus controller's hardware trigger, which the acquisition board
    records on a digital input line. Both are sample numbers on the same
    acquisition clock, so their difference covers everything from the samples
    reaching the computer to the controller's trigger: detection windowing,
    GUI buffering, the network request and the controller. It excludes the
    headstage-to-board and stimulator latencies, which are measured offline.

    The latency is reported from the TTL event's sample, which is the start of
    the RMS window that crossed threshold (what an offline comparison of the
    two lines measures), and also from the decision sample, the end of that
    window, the first sample at which the detector could have known.

    An onset with no hardware edge within the timeout counts as missed (no
    stimulus seen); a hardware edge with no onset before it counts as
    unmatched (e.g. a TEST trigger). Onsets are matched oldest first.

    Written on the audio thread, read by the UI: the statistics are atomics.
    Free of JUCE so Tests/ can drive it.
*/
class StimLatencyMeter
{
public:
    /** Clears all state; timeoutSamples is how long an onset waits for its hardware edge */
    void reset (double sampleRateHz, int64_t timeoutSamples)
    {
        sampleRate = sampleRateHz;
        timeout = timeoutSamples;
        pendingCount = 0;
        matched = 0;
        missed = 0;
        unmatched = 0;
        sumMs = 0.0;
        sumDecisionMs = 0.0;
        minMs = 0.0;
        maxMs = 0.0;
        lastMs = 0.0;
    }

    /** A ripple TTL went high: eventSample is its sample number, decisionSample the end of its window */
    void onsetEmitted (int64_t eventSample, int64_t decisionSample)
    {
        if (pendingCount == (int) pending.size())
        {
            // Full only if edges stopped arriving; the oldest has waited longest
            dropOldest();
            missed++;
        }
        pending[(size_t) pendingCount++] = { eventSample, decisionSample };
    }

    /** The hardware trigger line rose at this sample */
    void hardwareEdge (int64_t sample)
    {
        for (int i = 0; i < pendingCount; i++)
        {
            const Pending& p = pending[(size_t) i];
            if (sample < p.eventSample || sample - p.eventSample > timeout)
                continue;

            // Anything pending before this onset never got its own edge
            for (int k = 0; k < i; k++)
                dropOldest(), missed++;

            const Pending hit = pending[0];
            dropOldest();
            record (toMs (sample - hit.eventSample), toMs (sample - hit.decisionSample));
            return;
        }

        unmatched++;
    }

    /** Counts onsets that have waited longer than the timeout as missed */
    void expire (int64_t currentSample)
    {
        while (pendingCount > 0 && currentSample - pending[0].eventSample > timeout)
        {
            dropOldest();
            missed++;
        }
    }

    struct Stats
    {
        uint32_t matched; // onsets paired with a hardware edge
        uint32_t missed; // onsets with no hardware edge within the timeout
        uint32_t unmatched; // hardware edges with no onset before them
        double meanMs; // from the TTL event sample
        double minMs;
        double maxMs;
        double lastMs;
        double meanDecisionMs; // from the decision sample (end of the onset's window)
    };

    Stats getStats() const
    {
        const uint32_t n = matched;
        return { n, missed, unmatched,
                 n > 0 ? sumMs / n : 0.0, minMs, maxMs, lastMs,
                 n > 0 ? sumDecisionMs / n : 0.0 };
    }

private:
    struct Pending
    {
        int64_t eventSample;
        int64_t decisionSample;
    };

    double toMs (int64_t samples) const { return sampleRate > 0.0 ? 1000.0 * (double) samples / sampleRate : 0.0; }

    void dropOldest()
    {
        for (int i = 1; i < pendingCount; i++)
            pending[(size_t) (i - 1)] = pending[(size_t) i];
        pendingCount--;
    }

    void record (double ms, double decisionMs)
    {
        const uint32_t n = matched;
        minMs = n == 0 ? ms : std::min (minMs.load(), ms);
        maxMs = n == 0 ? ms : std::max (maxMs.load(), ms);
        lastMs = ms;
        sumMs = sumMs + ms;
        sumDecisionMs = sumDecisionMs + decisionMs;
        matched = n + 1;
    }

    double sampleRate { 30000.0 };
    int64_t timeout { 15000 };

    std::array<Pending, 16> pending {};
    int pendingCount { 0 };

    std::atomic<uint32_t> matched { 0 };
    std::atomic<uint32_t> missed { 0 };
    std::atomic<uint32_t> unmatched { 0 };
    std::atomic<double> sumMs { 0.0 };
    std::atomic<double> sumDecisionMs { 0.0 };
    std::atomic<double> minMs { 0.0 };
    std::atomic<double> maxMs { 0.0 };
    std::atomic<double> lastMs { 0.0 };
};

#endif
