/*
    Standalone tests for the periodic test mode's timer (Source/PeriodicSchedule.h).
*/

#include "PeriodicSchedule.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr float FS = 30000.0f;

int failures = 0;
int checks = 0;

void check (bool condition, const std::string& message)
{
    checks++;
    if (! condition)
    {
        failures++;
        std::printf ("  FAIL: %s\n", message.c_str());
    }
}

/** Absolute samples of the onsets over n samples, in blocks */
std::vector<int64_t> run (PeriodicSchedule& s, int64_t n, int block)
{
    std::vector<int64_t> all;
    std::vector<int> out;
    for (int64_t pos = 0; pos < n; pos += block)
    {
        const int len = (int) std::min<int64_t> (block, n - pos);
        out.clear();
        s.onsets (len, out);
        for (int o : out)
            all.push_back (pos + o);
    }
    return all;
}

std::string str (const std::vector<int64_t>& v)
{
    std::string s;
    for (auto x : v)
        s += std::to_string (x) + " ";
    return s;
}

void testSchedule()
{
    std::printf ("Schedule\n");
    for (int block : { 1, 333, 1024, 30000 })
    {
        PeriodicSchedule s;
        s.configure (FS, 3, 2.0);
        s.reset();
        const auto onsets = run (s, 10 * 30000, block);
        check (onsets == std::vector<int64_t> ({ 60000, 120000, 180000 }),
               "block " + std::to_string (block) + ": 3 onsets, 2 s apart, the first 2 s in (" + str (onsets) + ")");
        check (s.isDone() && s.getSent() == 3, "block " + std::to_string (block) + ": stops after the count");
    }

    // Sub-second intervals
    PeriodicSchedule fast;
    fast.configure (FS, 4, 0.1);
    fast.reset();
    check (run (fast, 30000, 512) == std::vector<int64_t> ({ 3000, 6000, 9000, 12000 }), "0.1 s interval");

    // Count 0: nothing
    PeriodicSchedule none;
    none.configure (FS, 0, 1.0);
    none.reset();
    check (run (none, 5 * 30000, 1024).empty() && none.isDone(), "count 0: no onsets");
}

void testResetAndReconfigure()
{
    std::printf ("Reset and reconfigure\n");
    PeriodicSchedule s;
    s.configure (FS, 2, 1.0);
    s.reset();
    run (s, 3 * 30000, 1024);
    check (s.getSent() == 2, "first run sends 2");

    // Reset restarts the sequence, one interval from the reset
    s.reset();
    check (run (s, 30001, 30001) == std::vector<int64_t> ({ 30000 }), "after reset: the first onset one interval in");

    // A shorter interval set mid-sequence applies from the next onset
    PeriodicSchedule t;
    t.configure (FS, 3, 10.0);
    t.reset();
    run (t, 30000, 1024); // 1 s in, next onset due at 10 s
    t.configure (FS, 3, 2.0); // now due by 1 s + 2 s = 3 s
    const auto later = run (t, 10 * 30000, 1024);
    check (later == std::vector<int64_t> ({ 60000, 120000, 180000 }),
           "shorter interval mid-sequence: next onset 2 s after the change, then every 2 s (" + str (later) + ")");

    // Raising the count after the sequence finished sends the extra onsets
    PeriodicSchedule u;
    u.configure (FS, 1, 1.0);
    u.reset();
    run (u, 3 * 30000, 1024);
    u.configure (FS, 2, 1.0);
    check (u.getSent() == 1 && ! u.isDone(), "count raised: not done");
}
} // namespace

int main()
{
    testSchedule();
    testResetAndReconfigure();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
