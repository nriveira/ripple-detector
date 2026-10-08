#ifndef __RIPPLE_TRIGGER_LOCKOUT_H
#define __RIPPLE_TRIGGER_LOCKOUT_H

#include <atomic>
#include <cstdint>

/**
    One stimulus per event: after a trigger is sent, further triggers are dropped
    until the lockout has passed. Time is the caller's clock in ticks (the laser
    trigger uses the high-resolution timer, so the lockout holds across streams,
    whose sample clocks differ).

    A stimulus can come back as an artefact on the electrode and be detected
    again, and with the stimulus triggered by that detection the loop keeps
    itself going as a pulse train. The lockout breaks the loop at the stimulus.

    Free of JUCE so Tests/ can drive it. allow() is for one thread (the audio
    thread); setLockout() may be called from another.
*/
class TriggerLockout
{
public:
    /** Lockout length in ticks; 0 turns it off */
    void setLockout (int64_t ticks) { lockoutTicks = ticks > 0 ? ticks : 0; }

    /** True if a trigger may be sent now (and starts the lockout); false if it falls in the lockout */
    bool allow (int64_t now)
    {
        const int64_t lockout = lockoutTicks.load();
        if (lockout > 0 && hasSent && now - lastSent < lockout)
        {
            suppressed++;
            return false;
        }

        hasSent = true;
        lastSent = now;
        return true;
    }

    /** Triggers dropped in the lockout */
    uint32_t getSuppressed() const { return suppressed; }

private:
    std::atomic<int64_t> lockoutTicks { 0 };
    bool hasSent { false };
    int64_t lastSent { 0 };
    std::atomic<uint32_t> suppressed { 0 };
};

#endif
