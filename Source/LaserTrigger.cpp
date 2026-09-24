#include "LaserTrigger.h"

namespace
{
constexpr int CONNECT_TIMEOUT_MS = 250;
constexpr int REPLY_TIMEOUT_MS = 1000; // the broker itself allows 1 s for its reply
constexpr int MAX_REPLY_BYTES = 2048;
} // namespace

LaserTrigger::LaserTrigger() : Thread ("Laser Trigger")
{
    startThread();
}

LaserTrigger::~LaserTrigger()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (CONNECT_TIMEOUT_MS + REPLY_TIMEOUT_MS + 500);
}

bool LaserTrigger::configure (const String& host, int port)
{
    const String trimmed = host.trim();
    const bool valid = LaserTriggerHttp::isNumericIPv4 (trimmed.toStdString()) && port > 0 && port < 65536;

    const ScopedLock sl (destinationLock);
    host_ = trimmed;
    port_ = port;
    valid_ = valid;
    return valid;
}

void LaserTrigger::fire()
{
    if (! isActive())
        return;

    requested_++;
    wake.signal();
}

bool LaserTrigger::test()
{
    if (! valid_)
        return false;

    testsRequested_++;
    wake.signal();
    return true;
}

LaserTrigger::Stats LaserTrigger::getStats() const
{
    const uint32_t fired = fired_;
    const uint32_t rejected = rejected_;
    const uint32_t answered = fired + rejected;
    return { requested_, fired, rejected, failed_, answered > 0 ? totalMs_ / answered : 0.0, maxMs_ };
}

LaserTriggerHttp::Reply LaserTrigger::postTimed (double& ms)
{
    String host;
    int port;
    {
        const ScopedLock sl (destinationLock);
        host = host_;
        port = port_;
    }

    return post (host, port, ms);
}

void LaserTrigger::run()
{
    while (! threadShouldExit())
    {
        wake.wait (100);

        while (testsCompleted_ != testsRequested_.load() && ! threadShouldExit())
        {
            double ms = 0.0;
            testReply_ = (int) postTimed (ms);
            testMs_ = ms;
            testsCompleted_++;
        }

        // One request per fire(), in order. A backlog only builds if requests
        // arrive faster than the Pi answers; each one is still sent, so the
        // counts always add up to `requested`.
        while (handled_ != requested_.load() && ! threadShouldExit())
        {
            handled_++;

            double ms = 0.0;
            const auto reply = postTimed (ms);

            if (reply == LaserTriggerHttp::Reply::Fired || reply == LaserTriggerHttp::Reply::Rejected)
            {
                (reply == LaserTriggerHttp::Reply::Fired ? fired_ : rejected_)++;
                totalMs_ = totalMs_ + ms;
                if (ms > maxMs_)
                    maxMs_ = ms;
            }
            else
            {
                failed_++;
            }
        }
    }
}

LaserTriggerHttp::Reply LaserTrigger::post (const String& host, int port, double& firstByteMs)
{
    firstByteMs = 0.0;

    StreamingSocket socket;
    if (! socket.connect (host, port, CONNECT_TIMEOUT_MS))
        return LaserTriggerHttp::Reply::Malformed;

    const std::string request = LaserTriggerHttp::buildRequest (host.toStdString(), port);
    const int64 sent = Time::getHighResolutionTicks();
    if (socket.write (request.data(), (int) request.size()) != (int) request.size())
        return LaserTriggerHttp::Reply::Malformed;

    // Read until the declared length has arrived (or the server closes), not
    // until the close itself: see LaserTriggerHttp.h
    std::string response;
    char buffer[512];

    while ((int) response.size() < MAX_REPLY_BYTES && ! LaserTriggerHttp::isComplete (response))
    {
        if (socket.waitUntilReady (true, REPLY_TIMEOUT_MS) != 1)
            break;

        const int n = socket.read (buffer, (int) sizeof (buffer), false);
        if (n <= 0)
            break;

        if (response.empty())
            firstByteMs = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - sent) * 1000.0;

        response.append (buffer, (size_t) n);
    }

    return LaserTriggerHttp::parseReply (response);
}
