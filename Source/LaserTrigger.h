#ifndef __LASER_TRIGGER_H
#define __LASER_TRIGGER_H

#include <ProcessorHeaders.h>
#include <atomic>
#include <cstdint>

#include "LaserTriggerPacket.h"

/**
    Sends one UDP datagram per ripple onset to the LaserDriver Pi broker, which
    fires its GPIO trigger (LaserDriver Pi/udp_trigger.py). The datagram layout
    is in LaserTriggerPacket.h; the broker counts gaps in its sequence number,
    so a lost datagram is visible there.

    configure() runs on the message thread while acquisition is stopped;
    fire() runs on the audio thread and makes one sendto() with no allocation.
*/
class LaserTrigger
{
public:
    LaserTrigger();

    /** Sets the destination. Returns false (and disables sending) unless host is a numeric IPv4 address. */
    bool configure (const String& host, int port);

    /** Turns sending on or off; safe to call during acquisition. */
    void setEnabled (bool enabled) { enabled_ = enabled; }

    /** True when enabled and the destination is valid. */
    bool isActive() const { return enabled_ && valid_; }

    /** Sends one trigger. Returns true if the datagram was handed to the network stack. */
    bool fire (int64 sampleNumber);

    uint32_t getSent() const { return sent_; }
    uint32_t getFailed() const { return failed_; }

private:
    DatagramSocket socket_;
    String host_;
    int port_ { LaserTriggerPacket::DEFAULT_PORT };
    bool valid_ { false };
    std::atomic<bool> enabled_ { false };
    uint32_t seq_ { 0 };
    std::atomic<uint32_t> sent_ { 0 };
    std::atomic<uint32_t> failed_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaserTrigger);
};

#endif
