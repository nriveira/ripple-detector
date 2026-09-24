/*
    Standalone tests for the laser trigger datagram (Source/LaserTriggerPacket.h).

    The expected bytes are the same vector LaserDriver's Pi/tests/test_udp_trigger.py
    checks against its decoder, so the two sides agree on the wire format.

        c++ -std=c++17 -I ../Source LaserTriggerPacketTests.cpp -o laser_trigger_tests
        ./laser_trigger_tests
*/

#include "LaserTriggerPacket.h"

#include <cstdio>
#include <cstring>

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

void testPackLayout()
{
    uint8_t out[LaserTriggerPacket::SIZE];
    LaserTriggerPacket::pack (out, 0x01020304u, 0x0A0B0C0D0E0F1011ull);

    const uint8_t expected[LaserTriggerPacket::SIZE] = {
        'L', 'T', 'R', '1',
        0x04, 0x03, 0x02, 0x01,
        0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A
    };
    check (std::memcmp (out, expected, sizeof (expected)) == 0, "pack: magic, little-endian seq and sample");
}

void testNumericIPv4()
{
    check (LaserTriggerPacket::isNumericIPv4 ("192.168.18.42"), "accepts a dotted quad");
    check (LaserTriggerPacket::isNumericIPv4 ("0.0.0.0"), "accepts zeros");
    check (LaserTriggerPacket::isNumericIPv4 ("255.255.255.255"), "accepts 255s");
    check (! LaserTriggerPacket::isNumericIPv4 (""), "rejects empty");
    check (! LaserTriggerPacket::isNumericIPv4 ("laserpi.local"), "rejects a hostname");
    check (! LaserTriggerPacket::isNumericIPv4 ("192.168.18"), "rejects three parts");
    check (! LaserTriggerPacket::isNumericIPv4 ("192.168.18.1.5"), "rejects five parts");
    check (! LaserTriggerPacket::isNumericIPv4 ("192.168.18.256"), "rejects an octet over 255");
    check (! LaserTriggerPacket::isNumericIPv4 ("192..18.1"), "rejects an empty octet");
    check (! LaserTriggerPacket::isNumericIPv4 ("192.168.18."), "rejects a trailing dot");
    check (! LaserTriggerPacket::isNumericIPv4 ("1922.168.18.1"), "rejects four digits");
    check (! LaserTriggerPacket::isNumericIPv4 (" 192.168.18.1"), "rejects whitespace");
}
} // namespace

int main()
{
    testPackLayout();
    testNumericIPv4();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
