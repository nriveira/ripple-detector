#include "LaserTrigger.h"

namespace
{
constexpr int CONNECT_TIMEOUT_MS = 250;
constexpr int REPLY_TIMEOUT_MS = 1000; // HTTP: the Pi's web GUI allows its broker 1 s
constexpr int MAX_REPLY_BYTES = 2048;
constexpr int UDP_TEST_TIMEOUT_MS = 250;
constexpr int UDP_POLL_MS = 50; // how often the sender thread looks for TEST requests while reading acknowledgements

double ticksToMs (int64 ticks)
{
    return Time::highResolutionTicksToSeconds (ticks) * 1000.0;
}

/** Microseconds from the Pi's receive stamp to its edge stamp, 0 if not meaningful */
double piMicros (const LaserTriggerUdp::Reply& r)
{
    return r.edgeNs > r.rxNs ? (double) (r.edgeNs - r.rxNs) / 1000.0 : 0.0;
}
} // namespace

LaserTrigger::LaserTrigger() : Thread ("Laser Trigger"),
                               udpSocket (false),
                               testSocket (false),
                               // The broker suppresses a repeated (sender, seq) within 1 s; random
                               // starts keep a restarted plugin clear of its previous run's numbers
                               udpSeq_ ((uint32_t) Random::getSystemRandom().nextInt()),
                               testSeq_ ((uint32_t) Random::getSystemRandom().nextInt())
{
    startThread();
}

LaserTrigger::~LaserTrigger()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (CONNECT_TIMEOUT_MS + REPLY_TIMEOUT_MS + 500);
}

bool LaserTrigger::configure (const String& host, int port, Transport transport)
{
    const String trimmed = host.trim();
    const bool valid = LaserTriggerHttp::isNumericIPv4 (trimmed.toStdString()) && port > 0 && port < 65536;

    {
        const ScopedLock sl (destinationLock);
        host_ = trimmed;
        port_ = port;
        transport_ = transport;
        valid_ = valid;
    }

    // A PING through the stimulation socket makes the socket resolve and cache
    // the destination here, so the audio thread's first trigger does not. The
    // broker answers without touching the GPIO; the answer is ignored.
    if (valid && transport == Transport::Udp)
    {
        uint8_t ping[LaserTriggerUdp::REQUEST_SIZE];
        LaserTriggerUdp::packRequest (ping, LaserTriggerUdp::PING, 0, 0);
        udpSocket.write (trimmed, port, ping, LaserTriggerUdp::REQUEST_SIZE);
    }

    return valid;
}

void LaserTrigger::fire()
{
    if (! isActive())
        return;

    requested_++;

    if (transport_ == Transport::Udp)
    {
        // client_ts carries the send time; the broker echoes it, so the round
        // trip needs no bookkeeping here
        uint8_t packet[LaserTriggerUdp::REQUEST_SIZE];
        LaserTriggerUdp::packRequest (packet, LaserTriggerUdp::TRIGGER, udpSeq_++, (uint64_t) Time::getHighResolutionTicks());
        udpSocket.write (host_, port_, packet, LaserTriggerUdp::REQUEST_SIZE);
    }
    else
    {
        wake.signal();
    }
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
    const uint32_t requested = requested_;
    const uint32_t answered = answered_;
    const uint32_t fired = fired_;

    return { requested,
             fired,
             busy_,
             rejected_,
             requested > answered ? requested - answered : 0,
             answered > 0 ? totalMs_ / answered : 0.0,
             maxMs_,
             fired > 0 ? totalPiUs_ / fired : 0.0,
             maxPiUs_ };
}

void LaserTrigger::recordAnswer (bool fired, bool busy, double ms, double piUs)
{
    (fired ? fired_ : rejected_)++;
    if (busy)
        busy_++;

    totalMs_ = totalMs_ + ms;
    if (ms > maxMs_)
        maxMs_ = ms;

    if (fired && piUs > 0.0)
    {
        totalPiUs_ = totalPiUs_ + piUs;
        if (piUs > maxPiUs_)
            maxPiUs_ = piUs;
    }

    answered_++;
}

void LaserTrigger::run()
{
    while (! threadShouldExit())
    {
        String host;
        int port;
        Transport transport;
        {
            const ScopedLock sl (destinationLock);
            host = host_;
            port = port_;
            transport = transport_;
        }

        if (transport == Transport::Udp)
            readUdpAck (UDP_POLL_MS);
        else
            wake.wait (100);

        while (testsCompleted_ != testsRequested_.load() && ! threadShouldExit())
        {
            if (transport == Transport::Udp)
            {
                runUdpTest (host, port);
            }
            else
            {
                double ms = 0.0;
                const auto reply = post (host, port, ms);
                testOutcome_ = (int) (reply == LaserTriggerHttp::Reply::Fired ? Outcome::Fired
                                      : reply == LaserTriggerHttp::Reply::Rejected ? Outcome::Rejected
                                                                                     : Outcome::NoLink);
                testMs_ = ms;
                testPiUs_ = 0.0;
            }
            testsCompleted_++;
        }

        if (transport != Transport::Http)
            continue;

        // HTTP: one request per fire(), in order. A backlog only builds if
        // requests arrive faster than the Pi answers; each one is still sent.
        while (handled_ != requested_.load() && ! threadShouldExit())
        {
            handled_++;

            double ms = 0.0;
            const auto reply = post (host, port, ms);

            if (reply == LaserTriggerHttp::Reply::Fired || reply == LaserTriggerHttp::Reply::Rejected)
                recordAnswer (reply == LaserTriggerHttp::Reply::Fired, false, ms, 0.0);
        }
    }
}

void LaserTrigger::readUdpAck (int timeoutMs)
{
    if (udpSocket.waitUntilReady (true, timeoutMs) != 1)
        return;

    uint8_t buffer[64];
    String senderIp;
    int senderPort = 0;
    const int n = udpSocket.read (buffer, (int) sizeof (buffer), false, senderIp, senderPort);

    LaserTriggerUdp::Reply r;
    if (n <= 0 || ! LaserTriggerUdp::parseReply (buffer, n, r) || r.status == LaserTriggerUdp::PONG)
        return;

    const double ms = ticksToMs (Time::getHighResolutionTicks() - (int64) r.clientTs);
    recordAnswer (r.status == LaserTriggerUdp::FIRED, (r.flags & LaserTriggerUdp::MCU_BUSY) != 0, ms, piMicros (r));
}

void LaserTrigger::runUdpTest (const String& host, int port)
{
    const uint32_t seq = testSeq_++;
    const int64 sent = Time::getHighResolutionTicks();

    uint8_t packet[LaserTriggerUdp::REQUEST_SIZE];
    LaserTriggerUdp::packRequest (packet, LaserTriggerUdp::TRIGGER, seq, (uint64_t) sent);

    testOutcome_ = (int) Outcome::NoLink;
    testMs_ = 0.0;
    testPiUs_ = 0.0;

    if (testSocket.write (host, port, packet, LaserTriggerUdp::REQUEST_SIZE) != LaserTriggerUdp::REQUEST_SIZE)
        return;

    const int64 deadline = sent + Time::secondsToHighResolutionTicks (UDP_TEST_TIMEOUT_MS / 1000.0);

    while (Time::getHighResolutionTicks() < deadline)
    {
        const int left = jmax (1, (int) ticksToMs (deadline - Time::getHighResolutionTicks()));
        if (testSocket.waitUntilReady (true, left) != 1)
            return;

        uint8_t buffer[64];
        String senderIp;
        int senderPort = 0;
        const int n = testSocket.read (buffer, (int) sizeof (buffer), false, senderIp, senderPort);

        LaserTriggerUdp::Reply r;
        if (n <= 0 || ! LaserTriggerUdp::parseReply (buffer, n, r) || r.seq != seq)
            continue; // an older test's late answer

        testMs_ = ticksToMs (Time::getHighResolutionTicks() - sent);
        testPiUs_ = piMicros (r);

        if (r.status != LaserTriggerUdp::FIRED)
            testOutcome_ = (int) Outcome::Rejected;
        else if (r.flags & LaserTriggerUdp::MCU_DOWN)
            testOutcome_ = (int) Outcome::McuDown;
        else if (r.flags & LaserTriggerUdp::MCU_BUSY)
            testOutcome_ = (int) Outcome::Busy;
        else
            testOutcome_ = (int) Outcome::Fired;
        return;
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
            firstByteMs = ticksToMs (Time::getHighResolutionTicks() - sent);

        response.append (buffer, (size_t) n);
    }

    return LaserTriggerHttp::parseReply (response);
}
