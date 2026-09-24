#ifndef __LASER_TRIGGER_PACKET_H
#define __LASER_TRIGGER_PACKET_H

#include <cstdint>
#include <string>

// Wire format of the laser trigger datagram. Free of JUCE so Tests/ can check
// it byte for byte; LaserDriver Pi/udp_trigger.py decodes the same layout
// (16 bytes, little-endian) -- change both together:
//
//     offset  size  field
//     0       4     magic   "LTR1"
//     4       4     seq     u32, +1 per datagram sent
//     8       8     sample  u64, sample number of the ripple onset event
namespace LaserTriggerPacket
{
constexpr int SIZE = 16;
constexpr int DEFAULT_PORT = 27136; // 0x6A00

inline void pack (uint8_t* out, uint32_t seq, uint64_t sampleNumber)
{
    out[0] = 'L';
    out[1] = 'T';
    out[2] = 'R';
    out[3] = '1';
    for (int i = 0; i < 4; i++)
        out[4 + i] = (uint8_t) (seq >> (8 * i));
    for (int i = 0; i < 8; i++)
        out[8 + i] = (uint8_t) (sampleNumber >> (8 * i));
}

/** True for a dotted-quad IPv4 address, so sending never waits on a name lookup. */
inline bool isNumericIPv4 (const std::string& host)
{
    int parts = 0;
    int digits = 0;
    int value = 0;

    for (char c : host)
    {
        if (c >= '0' && c <= '9')
        {
            if (++digits > 3)
                return false;
            value = value * 10 + (c - '0');
            if (value > 255)
                return false;
        }
        else if (c == '.')
        {
            if (digits == 0)
                return false;
            parts++;
            digits = 0;
            value = 0;
        }
        else
        {
            return false;
        }
    }

    return parts == 3 && digits > 0;
}
} // namespace LaserTriggerPacket

#endif
