#ifndef __LASER_TRIGGER_H
#define __LASER_TRIGGER_H

#include <ProcessorHeaders.h>
#include <atomic>
#include <cstdint>

#include "LaserTriggerHttp.h"

/**
    Fires the laser through the LaserDriver Pi's web API on every ripple onset
    (POST /api/trigger_gpio, see LaserTriggerHttp.h).

    An HTTP request blocks for a connect and a reply, so it cannot run on the
    audio thread. fire() only counts the request and wakes a sender thread;
    the thread makes one request per fire() and times it to the first byte of
    the Pi's reply, which the Pi only writes after firing, so the stats are a
    measured upper bound on the request-to-fire delay.

    configure() runs on the message thread while acquisition is stopped.
*/
class LaserTrigger : private Thread
{
public:
    LaserTrigger();
    ~LaserTrigger() override;

    /** Sets the destination. Returns false (and disables sending) unless host is a numeric IPv4 address. */
    bool configure (const String& host, int port);

    /** Turns sending on or off; safe to call during acquisition. */
    void setEnabled (bool enabled) { enabled_ = enabled; }

    /** True when enabled and the destination is valid. */
    bool isActive() const { return enabled_ && valid_; }

    /** Requests one trigger. Audio-thread safe: no allocation, no network I/O. */
    void fire();

    /**
        Requests one trigger from the editor's TEST button, whether or not
        sending is enabled, to check the connection. Returns false if the
        destination is not valid. The test is done when getTestsCompleted()
        catches up with getTestsRequested(); then read getTestReply().
    */
    bool test();

    uint32_t getTestsRequested() const { return testsRequested_; }
    uint32_t getTestsCompleted() const { return testsCompleted_; }

    /** Outcome and request-to-first-reply-byte time of the most recent test */
    LaserTriggerHttp::Reply getTestReply() const { return (LaserTriggerHttp::Reply) testReply_.load(); }
    double getTestMs() const { return testMs_; }

    struct Stats
    {
        uint32_t requested; // fire() calls while active
        uint32_t fired; // Pi answered "ok": true
        uint32_t rejected; // Pi answered "ok": false (up, but did not fire)
        uint32_t failed; // no connection, timeout, or unexpected reply
        double meanMs; // request to first reply byte, over answered requests
        double maxMs;
    };

    Stats getStats() const;

private:
    void run() override;

    /** One HTTP request; returns the Pi's answer (Malformed on any socket failure) and the time to its first byte. */
    LaserTriggerHttp::Reply post (const String& host, int port, double& firstByteMs);

    CriticalSection destinationLock; // host_/port_ between configure() and the sender thread
    String host_;
    int port_ { LaserTriggerHttp::DEFAULT_PORT };
    std::atomic<bool> valid_ { false };
    std::atomic<bool> enabled_ { false };

    WaitableEvent wake;
    std::atomic<uint32_t> requested_ { 0 };
    uint32_t handled_ { 0 }; // sender thread only

    std::atomic<uint32_t> fired_ { 0 };
    std::atomic<uint32_t> rejected_ { 0 };
    std::atomic<uint32_t> failed_ { 0 };
    std::atomic<double> totalMs_ { 0.0 };
    std::atomic<double> maxMs_ { 0.0 };

    // Tests are counted apart from stimulation so they never skew the run's stats
    std::atomic<uint32_t> testsRequested_ { 0 };
    std::atomic<uint32_t> testsCompleted_ { 0 };
    std::atomic<int> testReply_ { (int) LaserTriggerHttp::Reply::Malformed };
    std::atomic<double> testMs_ { 0.0 };

    /** Copies the destination under the lock and makes one request */
    LaserTriggerHttp::Reply postTimed (double& ms);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaserTrigger);
};

#endif
