/*
    Standalone tests for the stimulation lockout (Source/TriggerLockout.h).
*/

#include "TriggerLockout.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
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

/** Which of the requests at the given times are sent */
std::vector<bool> run (TriggerLockout& l, const std::vector<int64_t>& times)
{
    std::vector<bool> sent;
    for (int64_t t : times)
        sent.push_back (l.allow (t));
    return sent;
}

void testLockout()
{
    std::printf ("Lockout\n");

    // 500-tick lockout: a train of re-detections every 150 ticks gets one trigger per 500
    TriggerLockout l;
    l.setLockout (500);
    const auto sent = run (l, { 1000, 1150, 1300, 1450, 1600, 1750 });
    check (sent == std::vector<bool> ({ true, false, false, false, true, false }), "one trigger per lockout");
    check (l.getSuppressed() == 4, "suppressed counted");

    // The lockout runs from the last trigger sent, not from the last request
    TriggerLockout m;
    m.setLockout (500);
    check (run (m, { 0, 499, 500, 999, 1000 }) == std::vector<bool> ({ true, false, true, false, true }),
           "exactly one lockout from each trigger sent");

    // The first request is always sent, whatever the clock reads
    TriggerLockout first;
    first.setLockout (500);
    check (first.allow (-200) && first.allow (-200 + 500), "first trigger at any time");

    // 0 turns it off
    TriggerLockout off;
    off.setLockout (0);
    check (run (off, { 0, 1, 2 }) == std::vector<bool> ({ true, true, true }) && off.getSuppressed() == 0, "lockout 0: all sent");

    // Changing the lockout applies to the next request
    TriggerLockout change;
    change.setLockout (1000);
    change.allow (0);
    change.setLockout (100);
    check (change.allow (200), "a shorter lockout applies at once");
}
} // namespace

int main()
{
    testLockout();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
