#ifndef __LASER_TRIGGER_UDP_H
#define __LASER_TRIGGER_UDP_H

#include <cstdint>
#include <cstring>

// LaserDriver's network trigger (Pi/brokerd/udp_trigger.h, mirrored by
// Pi/udp_trigger.py): one datagram per trigger to laserhat-brokerd, which
// raises the Pi's GPIO 24 -> MSPM0 PA19 edge, then acknowledges.
//
// Request (20 B): magic "LHTR" | version u8 | type u8 | flags u16 (0) |
//                 seq u32 | client_ts u64 (opaque, echoed back)
// Reply   (36 B): magic | version u8 | type u8 (ACK) | status u8 | flags u8 |
//                 seq u32 | client_ts u64 | rx_ns u64 | edge_ns u64
// All little-endian. rx_ns / edge_ns are the Pi's clock when the datagram
// arrived and when the line went high, so edge_ns - rx_ns is the time spent
// inside the Pi. The reply is sent after the edge.
//
// Delivery is at-most-once: nothing is resent, because a late pulse is
// worse than a missing one.
//
// Free of JUCE so Tests/ can check it byte for byte.
namespace LaserTriggerUdp
{
constexpr int DEFAULT_PORT = 17017;
constexpr int REQUEST_SIZE = 20;
constexpr int REPLY_SIZE = 36;

constexpr uint32_t MAGIC = 0x5254484Cu; // bytes "LHTR"
constexpr uint8_t VERSION = 1;

constexpr uint8_t TRIGGER = 0x01;
constexpr uint8_t PING = 0x02; // answered without touching the GPIO
constexpr uint8_t ACK = 0x81;

enum Status : uint8_t
{
    FIRED = 0,
    DUPLICATE = 1, // this seq already fired; nothing new happened
    PONG = 2,
    BAD_VERSION = 3,
    BAD_TYPE = 4,
    NO_GPIO = 5 // the broker has no trigger line: nothing fired
};

enum Flags : uint8_t
{
    MCU_BUSY = 0x01, // the MCU was mid-pulse, so it ignores this edge
    MCU_DOWN = 0x02, // the broker has had no MCU status for 1.5 s
    GPIO_SIM = 0x04, // simulated GPIO (off-hardware testing)
    RX_TS_USER = 0x08 // rx_ns is a userspace stamp, not the kernel's
};

struct Reply
{
    uint8_t status;
    uint8_t flags;
    uint32_t seq;
    uint64_t clientTs;
    uint64_t rxNs;
    uint64_t edgeNs;
};

namespace detail
{
    inline void put (uint8_t* p, uint64_t v, int bytes)
    {
        for (int i = 0; i < bytes; i++)
            p[i] = (uint8_t) (v >> (8 * i));
    }

    inline uint64_t get (const uint8_t* p, int bytes)
    {
        uint64_t v = 0;
        for (int i = 0; i < bytes; i++)
            v |= (uint64_t) p[i] << (8 * i);
        return v;
    }
} // namespace detail

inline void packRequest (uint8_t* out, uint8_t type, uint32_t seq, uint64_t clientTs)
{
    detail::put (out, MAGIC, 4);
    out[4] = VERSION;
    out[5] = type;
    detail::put (out + 6, 0, 2);
    detail::put (out + 8, seq, 4);
    detail::put (out + 12, clientTs, 8);
}

/** Decodes an acknowledgement; false if it is not one */
inline bool parseReply (const uint8_t* data, int size, Reply& out)
{
    if (size < REPLY_SIZE || detail::get (data, 4) != MAGIC || data[5] != ACK)
        return false;

    out.status = data[6];
    out.flags = data[7];
    out.seq = (uint32_t) detail::get (data + 8, 4);
    out.clientTs = detail::get (data + 12, 8);
    out.rxNs = detail::get (data + 20, 8);
    out.edgeNs = detail::get (data + 28, 8);
    return true;
}
} // namespace LaserTriggerUdp

#endif
