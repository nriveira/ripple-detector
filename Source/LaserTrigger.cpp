#include "LaserTrigger.h"

LaserTrigger::LaserTrigger() : socket_ (false)
{
}

bool LaserTrigger::configure (const String& host, int port)
{
    host_ = host.trim();
    port_ = port;
    valid_ = LaserTriggerPacket::isNumericIPv4 (host_.toStdString()) && port_ > 0 && port_ < 65536;
    return valid_;
}

bool LaserTrigger::fire (int64 sampleNumber)
{
    if (! isActive())
        return false;

    uint8_t packet[LaserTriggerPacket::SIZE];
    LaserTriggerPacket::pack (packet, seq_++, (uint64_t) sampleNumber);

    const bool ok = socket_.write (host_, port_, packet, LaserTriggerPacket::SIZE) == LaserTriggerPacket::SIZE;
    (ok ? sent_ : failed_)++;
    return ok;
}
