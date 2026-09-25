/*
    Standalone tests for the stimulation latency measurement (Source/StimLatencyMeter.h).

        c++ -std=c++17 -I ../Source StimLatencyMeterTests.cpp -o latency_meter_tests
        ./latency_meter_tests
*/

#include "StimLatencyMeter.h"

#include <cmath>
#include <cstdio>

namespace
{
int checks = 0;
int failures = 0;

void check (bool ok, const char* what)
{
    checks++;
    if (! ok)
    {
        failures++;
        std::printf ("FAIL: %s\n", what);
    }
}

bool near (double a, double b) { return std::fabs (a - b) < 1e-9; }

constexpr double FS = 30000.0;
constexpr int64_t TIMEOUT = 15000; // 500 ms

void testPairs()
{
    StimLatencyMeter m;
    m.reset (FS, TIMEOUT);

    // Onset at 30000 (window 128 -> decided at 30128), hardware edge 600 samples later = 20 ms
    m.onsetEmitted (30000, 30128);
    m.hardwareEdge (30600);
    // A second one 1 s later, 750 samples = 25 ms
    m.onsetEmitted (60000, 60128);
    m.hardwareEdge (60750);

    const auto st = m.getStats();
    check (st.matched == 2 && st.missed == 0 && st.unmatched == 0, "two onsets, two edges, both paired");
    check (near (st.meanMs, 22.5), "mean from the TTL event sample");
    check (near (st.minMs, 20.0) && near (st.maxMs, 25.0) && near (st.lastMs, 25.0), "min / max / last");
    check (near (st.meanDecisionMs, 22.5 - 128.0 / 30.0), "mean from the decision sample excludes the window");
}

void testMissedAndUnmatched()
{
    StimLatencyMeter m;
    m.reset (FS, TIMEOUT);

    m.hardwareEdge (1000); // TEST button: nothing pending
    m.onsetEmitted (30000, 30128); // stimulus never came
    m.expire (30000 + TIMEOUT); // not yet past the timeout
    check (m.getStats().missed == 0, "an onset is not missed before its timeout");
    m.expire (30000 + TIMEOUT + 1);

    const auto st = m.getStats();
    check (st.unmatched == 1, "an edge with no onset is unmatched");
    check (st.missed == 1, "an onset with no edge within the timeout is missed");
    check (st.matched == 0, "nothing paired");
}

void testEdgeTooLateIsNotPaired()
{
    StimLatencyMeter m;
    m.reset (FS, TIMEOUT);

    m.onsetEmitted (30000, 30128);
    m.hardwareEdge (30000 + TIMEOUT + 10); // after the timeout: not this onset's stimulus
    const auto st = m.getStats();
    check (st.matched == 0 && st.unmatched == 1, "an edge after the timeout is not paired");
}

void testOldestFirst()
{
    StimLatencyMeter m;
    m.reset (FS, TIMEOUT);

    // Two onsets 100 ms apart, both waiting; edges arrive for each in order
    m.onsetEmitted (30000, 30128);
    m.onsetEmitted (33000, 33128);
    m.hardwareEdge (30600); // 20 ms after the first
    m.hardwareEdge (33900); // 30 ms after the second

    const auto st = m.getStats();
    check (st.matched == 2 && st.missed == 0, "overlapping onsets pair in order");
    check (near (st.minMs, 20.0) && near (st.maxMs, 30.0), "each edge pairs with its own onset");
}

void testEdgeBeforeOnsetIsNotPaired()
{
    StimLatencyMeter m;
    m.reset (FS, TIMEOUT);

    m.hardwareEdge (29000); // before the onset: cannot be its stimulus
    m.onsetEmitted (30000, 30128);
    const auto st = m.getStats();
    check (st.matched == 0 && st.unmatched == 1, "an edge before the onset is unmatched");
}
} // namespace

int main()
{
    testPairs();
    testMissedAndUnmatched();
    testEdgeTooLateIsNotPaired();
    testOldestFirst();
    testEdgeBeforeOnsetIsNotPaired();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
