#ifndef __LASER_TRIGGER_H
#define __LASER_TRIGGER_H

#include <ProcessorHeaders.h>
#include <atomic>
#include <cstdint>

#include "LaserTriggerHttp.h"
#include "LaserTriggerUdp.h"

/**
    Fires the laser through the LaserDriver Pi on every ripple onset, over
    one of two transports:

    - UDP (default): a 20-byte datagram to laserhat-brokerd's trigger port
      (LaserTriggerUdp.h). fire() sends it directly from the audio thread,
      a single non-blocking sendto with no allocation, so no thread hand-off
      sits in the path. The broker acknowledges after raising the line; a
      sender thread reads the acknowledgements, which carry the Pi's own
      receive-to-edge time and whether the MCU was busy (it ignores a trigger
      mid-pulse).
    - HTTP: POST /api/trigger_gpio to the Pi's web GUI (LaserTriggerHttp.h),
      for a Pi without the UDP listener. The request blocks, so fire() only
      counts it and wakes the sender thread, which times it to the first byte
      of the reply (the Pi fires before replying).

    configure() runs on the message thread while acquisition is stopped; the
    destination is then constant for the audio thread.
*/
class LaserTrigger : private Thread
{
public:
    enum class Transport
    {
        Udp,
        Http
    };

    /** Result of a TEST trigger */
    enum class Outcome
    {
        Fired,
        Busy, // fired, but the MCU was mid-pulse and ignores it
        McuDown, // fired, but the broker has not heard from the MCU
        Rejected, // the Pi answered but did not fire
        NoLink // no answer
    };

    LaserTrigger();
    ~LaserTrigger() override;

    /** Sets the destination. Returns false (and disables sending) unless host is a numeric IPv4 address. */
    bool configure (const String& host, int port, Transport transport);

    /** Turns sending on or off; safe to call during acquisition. */
    void setEnabled (bool enabled) { enabled_ = enabled; }

    /** True when enabled and the destination is valid. */
    bool isActive() const { return enabled_ && valid_; }

    Transport getTransport() const { return transport_; }

    /** Fires one trigger. Audio-thread safe: no allocation and no blocking I/O. */
    void fire();

    /**
        Fires one trigger from the editor's TEST button, whether or not sending
        is enabled, to check the connection. Returns false if the destination
        is not valid. The test is done when getTestsCompleted() catches up with
        getTestsRequested(); then read the getTest...() results.
    */
    bool test();

    uint32_t getTestsRequested() const { return testsRequested_; }
    uint32_t getTestsCompleted() const { return testsCompleted_; }
    Outcome getTestOutcome() const { return (Outcome) testOutcome_.load(); }
    double getTestMs() const { return testMs_; } // round trip (UDP) or request to first reply byte (HTTP)
    double getTestPiUs() const { return testPiUs_; } // UDP only: Pi receive to GPIO edge

    struct Stats
    {
        uint32_t requested; // fire() calls while active
        uint32_t fired; // the Pi raised the line
        uint32_t busy; // of those, the MCU was mid-pulse and ignored it
        uint32_t rejected; // the Pi answered but did not fire
        uint32_t failed; // no answer (lost, refused, or still in flight)
        double meanMs; // round trip (UDP) or request to first reply byte (HTTP), over answered requests
        double maxMs;
        double meanPiUs; // UDP only: Pi receive to GPIO edge, over fired triggers
        double maxPiUs;
    };

    Stats getStats() const;

private:
    void run() override;

    /** UDP: reads and records one acknowledgement if one arrives within timeoutMs */
    void readUdpAck (int timeoutMs);

    /** UDP: one TEST trigger on its own socket, waiting for its acknowledgement */
    void runUdpTest (const String& host, int port);

    /** HTTP: one request; returns the Pi's answer (Malformed on any socket failure) and the time to its first byte */
    LaserTriggerHttp::Reply post (const String& host, int port, double& firstByteMs);

    /** Records one answered trigger */
    void recordAnswer (bool fired, bool busy, double ms, double piUs);

    CriticalSection destinationLock; // host_/port_/transport_ between configure() and the sender thread
    String host_;
    int port_ { LaserTriggerUdp::DEFAULT_PORT };
    std::atomic<Transport> transport_ { Transport::Udp };
    std::atomic<bool> valid_ { false };
    std::atomic<bool> enabled_ { false };

    DatagramSocket udpSocket; // stimulation: written by the audio thread, acknowledgements read by the sender thread
    DatagramSocket testSocket; // TEST triggers: sender thread only
    uint32_t udpSeq_; // audio thread only; random start, see the constructor
    uint32_t testSeq_; // sender thread only

    WaitableEvent wake;
    std::atomic<uint32_t> requested_ { 0 };
    uint32_t handled_ { 0 }; // HTTP: sender thread only

    std::atomic<uint32_t> fired_ { 0 };
    std::atomic<uint32_t> busy_ { 0 };
    std::atomic<uint32_t> rejected_ { 0 };
    std::atomic<uint32_t> answered_ { 0 };
    std::atomic<double> totalMs_ { 0.0 };
    std::atomic<double> maxMs_ { 0.0 };
    std::atomic<double> totalPiUs_ { 0.0 };
    std::atomic<double> maxPiUs_ { 0.0 };

    // Tests are counted apart from stimulation so they never skew the run's stats
    std::atomic<uint32_t> testsRequested_ { 0 };
    std::atomic<uint32_t> testsCompleted_ { 0 };
    std::atomic<int> testOutcome_ { (int) Outcome::NoLink };
    std::atomic<double> testMs_ { 0.0 };
    std::atomic<double> testPiUs_ { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaserTrigger);
};

#endif
