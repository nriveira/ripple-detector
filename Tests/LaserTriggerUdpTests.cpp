/*
    Standalone tests for the laser trigger datagrams (Source/LaserTriggerUdp.h),
    which talk to LaserDriver's laserhat-brokerd (Pi/brokerd/udp_trigger.h).

    The expected bytes were produced by LaserDriver's own Python mirror,
    Pi/udp_trigger.py (pack_request and its reply struct), so the two sides
    agree on the wire format.

        c++ -std=c++17 -I ../Source LaserTriggerUdpTests.cpp -o laser_trigger_udp_tests
        ./laser_trigger_udp_tests
*/

#include "LaserTriggerUdp.h"

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

// udp_trigger.pack_request(TRIGGER, 0x01020304, 0x0A0B0C0D0E0F1011)
const uint8_t REQUEST[20] = { 0x4C, 0x48, 0x54, 0x52, 0x01, 0x01, 0x00, 0x00, 0x04, 0x03,
                              0x02, 0x01, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A };

// _REP.pack(MAGIC, VERSION, ACK, status=fired, flags=MCU_BUSY|GPIO_SIM, seq=0x01020304,
//           client_ts=0x0A0B0C0D0E0F1011, rx=0x1112131415161718, edge=0x2122232425262728)
const uint8_t REPLY[36] = { 0x4C, 0x48, 0x54, 0x52, 0x01, 0x81, 0x00, 0x05, 0x04, 0x03, 0x02, 0x01,
                            0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x18, 0x17, 0x16, 0x15,
                            0x14, 0x13, 0x12, 0x11, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21 };

void testRequest()
{
    uint8_t out[LaserTriggerUdp::REQUEST_SIZE];
    LaserTriggerUdp::packRequest (out, LaserTriggerUdp::TRIGGER, 0x01020304u, 0x0A0B0C0D0E0F1011ull);
    check (std::memcmp (out, REQUEST, sizeof REQUEST) == 0, "request matches LaserDriver's pack_request");
}

void testReply()
{
    LaserTriggerUdp::Reply r {};
    check (LaserTriggerUdp::parseReply (REPLY, (int) sizeof REPLY, r), "reply decodes");
    check (r.status == LaserTriggerUdp::FIRED, "status");
    check (r.flags == (LaserTriggerUdp::MCU_BUSY | LaserTriggerUdp::GPIO_SIM), "flags");
    check (r.seq == 0x01020304u, "seq");
    check (r.clientTs == 0x0A0B0C0D0E0F1011ull, "client_ts echoed");
    check (r.rxNs == 0x1112131415161718ull && r.edgeNs == 0x2122232425262728ull, "rx_ns and edge_ns");

    check (! LaserTriggerUdp::parseReply (REPLY, 35, r), "short reply rejected");

    uint8_t bad[36];
    std::memcpy (bad, REPLY, sizeof bad);
    bad[0] = 'X';
    check (! LaserTriggerUdp::parseReply (bad, (int) sizeof bad, r), "wrong magic rejected");

    std::memcpy (bad, REPLY, sizeof bad);
    bad[5] = LaserTriggerUdp::TRIGGER;
    check (! LaserTriggerUdp::parseReply (bad, (int) sizeof bad, r), "a request is not a reply");
}
} // namespace

int main()
{
    testRequest();
    testReply();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
