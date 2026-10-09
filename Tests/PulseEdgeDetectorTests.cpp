/*
    Standalone tests for the pulse test mode's edge detector (Source/DetectionMethods/PulseEdgeDetector).

    The signals mimic test pulses picked up in saline or replayed from a recording: Gaussian
    noise of different levels and mains hum, square pulses of different amplitudes, optionally
    through an AC coupling (high-pass) like a sound card output, spikes and bursts, and a short
    artefact from the stimulation that follows each pulse (which the lockout must hide). Only
    rising edges count. Also external onsets, as the periodic test mode gives them.
*/

#include "DetectionMethods/PulseEdgeDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr float FS = 30000.0f;
constexpr double PI = 3.14159265358979323846;

int failures = 0;
int checks = 0;

void check (bool condition, const std::string& message)
{
    checks++;
    if (! condition)
    {
        failures++;
        std::printf ("  FAIL: %s\n", message.c_str());
    }
}

struct Pulse
{
    int64_t start;
    int64_t length;
    double amplitude; // signed, uV
};

struct Options
{
    double noiseUv = 10.0; // Gaussian noise SD
    double humUv = 20.0; // 60 Hz hum amplitude
    double highPassHz = 0.0; // > 0: AC coupling of the pulse source
    double artefactUv = 0.0; // > 0: a 2 ms artefact this far after each pulse
    int64_t artefactDelay = 600; // samples (20 ms)
    int64_t artefactLength = 60; // samples (2 ms)
    unsigned seed = 7;
};

std::vector<float> makeSignal (int64_t n, const std::vector<Pulse>& pulses, const Options& o)
{
    std::vector<double> source ((size_t) n, 0.0);
    for (const auto& p : pulses)
    {
        for (int64_t i = p.start; i < std::min (n, p.start + p.length); i++)
            source[(size_t) i] += p.amplitude;
        if (o.artefactUv != 0.0)
            for (int64_t i = p.start + o.artefactDelay; i < std::min (n, p.start + o.artefactDelay + o.artefactLength); i++)
                source[(size_t) i] += o.artefactUv;
    }

    // First-order high-pass, as a sound card's output coupling capacitor
    if (o.highPassHz > 0.0)
    {
        const double rc = 1.0 / (2.0 * PI * o.highPassHz);
        const double a = rc / (rc + 1.0 / FS);
        double prevIn = 0.0, prevOut = 0.0;
        for (auto& v : source)
        {
            const double out = a * (prevOut + v - prevIn);
            prevIn = v;
            prevOut = out;
            v = out;
        }
    }

    std::mt19937 rng (o.seed);
    std::normal_distribution<double> noise (0.0, o.noiseUv);
    std::vector<float> x ((size_t) n);
    for (int64_t i = 0; i < n; i++)
        x[(size_t) i] = (float) (source[(size_t) i] + noise (rng) + o.humUv * std::sin (2.0 * PI * 60.0 * i / FS) + 35.0);
    return x;
}

struct Onset
{
    int64_t decision;
    int64_t edge;
};

struct Result
{
    std::vector<Onset> onsets;
    std::vector<int64_t> offsets;
};

Result run (const std::vector<float>& x, PulseEdgeDetector::Params p, int block)
{
    PulseEdgeDetector d;
    d.setParams (p);
    d.reset();

    Result r;
    std::vector<PulseEdgeDetector::Event> events;
    for (int64_t pos = 0; pos < (int64_t) x.size(); pos += block)
    {
        const int len = (int) std::min<int64_t> (block, (int64_t) x.size() - pos);
        events.clear();
        d.process (x.data() + pos, len, events);
        for (const auto& e : events)
        {
            if (e.type == PulseEdgeDetector::Event::Onset)
                r.onsets.push_back ({ pos + e.sampleIndex, pos + e.sampleIndex - e.edgeOffset });
            else
                r.offsets.push_back (pos + e.sampleIndex);
        }
    }
    return r;
}

/** Like run(), but the pulses' edges are given as external onsets (absolute samples), as from a TTL line */
Result runExternal (const std::vector<float>& x, PulseEdgeDetector::Params p, int block, const std::vector<int64_t>& edges,
                    uint32_t* ignored = nullptr)
{
    PulseEdgeDetector d;
    d.setParams (p);
    d.reset();

    Result r;
    std::vector<PulseEdgeDetector::Event> events;
    std::vector<int> onsets;
    for (int64_t pos = 0; pos < (int64_t) x.size(); pos += block)
    {
        const int len = (int) std::min<int64_t> (block, (int64_t) x.size() - pos);
        onsets.clear();
        for (int64_t e : edges)
            if (e >= pos && e < pos + len)
                onsets.push_back ((int) (e - pos));

        events.clear();
        d.process (x.data() + pos, len, events, nullptr, nullptr, &onsets);
        for (const auto& e : events)
        {
            if (e.type == PulseEdgeDetector::Event::Onset)
                r.onsets.push_back ({ pos + e.sampleIndex, pos + e.sampleIndex - e.edgeOffset });
            else
                r.offsets.push_back (pos + e.sampleIndex);
        }
    }
    if (ignored != nullptr)
        *ignored = d.getIgnoredExternal();
    return r;
}

PulseEdgeDetector::Params defaults()
{
    PulseEdgeDetector::Params p;
    p.sampleRate = FS;
    p.thresholdSds = 8.0;
    p.minStepUv = 100.0;
    p.minWidthMs = 0.3;
    p.lockoutMs = 1000.0;
    p.ttlMs = 10.0;
    return p;
}

int confirmSamples() { return (int) std::lround (FS * defaults().minWidthMs / 1000.0); }

/** Pulses every 1.5 to 2.5 s (jittered), starting at 1 s */
std::vector<Pulse> pulseTrain (int64_t n, double amplitude, int64_t length, unsigned seed = 3)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> gap (1.5, 2.5);
    std::vector<Pulse> pulses;
    for (double t = 1.0; (int64_t) ((t + 0.1) * FS) < n; t += gap (rng))
        pulses.push_back ({ (int64_t) (t * FS), length, amplitude });
    return pulses;
}

/** Every pulse detected once, with the edge within 'tolerance' samples of the true start */
void checkOnePerPulse (const std::string& name, const Result& r, const std::vector<Pulse>& pulses, int64_t tolerance)
{
    check (r.onsets.size() == pulses.size(), name + ": one onset per pulse (" + std::to_string (r.onsets.size()) + " vs " + std::to_string (pulses.size()) + ")");

    int64_t worst = 0;
    for (size_t k = 0; k < std::min (r.onsets.size(), pulses.size()); k++)
    {
        worst = std::max (worst, std::llabs (r.onsets[k].edge - pulses[k].start));
        check (r.onsets[k].decision >= r.onsets[k].edge, name + ": decision not before the edge");
    }
    check (worst <= tolerance, name + ": edge within " + std::to_string (tolerance) + " samples (worst " + std::to_string (worst) + ")");
}

// ---------------------------------------------------------------------------

void testRisingOnly()
{
    std::printf ("Rising edges only\n");
    const int64_t n = (int64_t) (40 * FS);

    const auto up = pulseTrain (n, 1000.0, 300); // 10 ms pulses
    const auto r = run (makeSignal (n, up, {}), defaults(), 1024);
    checkOnePerPulse ("rising", r, up, 1);
    check (r.offsets.size() == r.onsets.size(), "rising: every TTL pulse ends");
    for (size_t k = 0; k < std::min (r.onsets.size(), r.offsets.size()); k++)
        check (r.offsets[k] - r.onsets[k].decision == 300, "rising: TTL lasts 10 ms");
    for (const auto& o : r.onsets)
        check (o.decision - o.edge == confirmSamples() - 1, "rising: decided after Min Width");

    // Inverted pulses: their leading edge falls, and their trailing edge rises only back to the baseline
    const auto down = pulseTrain (n, -1000.0, 300);
    const auto r2 = run (makeSignal (n, down, {}), defaults(), 1024);
    check (r2.onsets.empty(), "falling: negative pulses are not detected (" + std::to_string (r2.onsets.size()) + ")");
    std::printf ("  %zu rising pulses -> %zu onsets; %zu falling pulses -> %zu onsets\n", up.size(), r.onsets.size(), down.size(), r2.onsets.size());
}

void testNoiseLevels()
{
    std::printf ("Noise levels: the threshold follows the noise\n");
    const int64_t n = (int64_t) (60 * FS);

    for (double sd : { 3.0, 10.0, 30.0, 100.0, 300.0 })
    {
        Options o;
        o.noiseUv = sd;
        o.humUv = sd; // hum as large as the noise
        o.seed = (unsigned) sd;

        // Noise alone: nothing
        const auto quiet = run (makeSignal (n, {}, o), defaults(), 1024);
        check (quiet.onsets.empty(), "noise " + std::to_string ((int) sd) + " uV: no false detections (" + std::to_string (quiet.onsets.size()) + ")");

        // Pulses 20x the noise (at least 300 uV, above the 100 uV Min Step)
        const double amp = std::max (300.0, 20.0 * sd);
        const auto pulses = pulseTrain (n, amp, 30); // 1 ms, like the test recording
        const auto r = run (makeSignal (n, pulses, o), defaults(), 1024);
        checkOnePerPulse ("noise " + std::to_string ((int) sd) + " uV, pulses " + std::to_string ((int) amp) + " uV", r, pulses, 2);
        std::printf ("  noise %5.0f uV: %zu false; %zu/%zu pulses of %.0f uV\n", sd, quiet.onsets.size(), r.onsets.size(), pulses.size(), amp);
    }
}

void testMixedAmplitudes()
{
    std::printf ("Pulses of different amplitudes in one recording\n");
    const int64_t n = (int64_t) (60 * FS);
    auto pulses = pulseTrain (n, 1.0, 30);
    const double amps[] = { 250.0, 400.0, 800.0, 1500.0, 3000.0, 6000.0 };
    for (size_t k = 0; k < pulses.size(); k++)
        pulses[k].amplitude = amps[k % 6];

    Options o;
    o.noiseUv = 20.0;
    o.humUv = 20.0;
    const auto r = run (makeSignal (n, pulses, o), defaults(), 1024);
    checkOnePerPulse ("mixed 250-6000 uV on 20 uV noise", r, pulses, 2);

    // Pulses well under the threshold (8 SD of ~25 uV noise and hum) are not detected
    auto small = pulseTrain (n, 80.0, 30);
    const auto r2 = run (makeSignal (n, small, o), defaults(), 1024);
    check (r2.onsets.empty(), "pulses of 80 uV in 20 uV noise: below threshold (" + std::to_string (r2.onsets.size()) + ")");
}

void testSpikesAndBursts()
{
    std::printf ("Spikes, bursts and impulsive noise\n");
    const int64_t n = (int64_t) (30 * FS);
    const int width = confirmSamples();

    // Spikes and bursts shorter than Min Width, far above threshold
    auto x = makeSignal (n, {}, {});
    int64_t t = (int64_t) FS;
    for (int len : { 1, 2, 3, width / 2, width - 3 })
        for (int rep = 0; rep < 5; rep++, t += (int64_t) (0.5 * FS))
            for (int k = 0; k < len; k++)
                x[(size_t) (t + k)] += 3000.0f;
    PulseEdgeDetector d;
    d.setParams (defaults());
    d.reset();
    std::vector<PulseEdgeDetector::Event> events;
    for (int64_t pos = 0; pos < n; pos += 1024)
        d.process (x.data() + pos, (int) std::min<int64_t> (1024, n - pos), events);
    check (events.empty(), "spikes and bursts up to Min Width - 3 samples: ignored (" + std::to_string (events.size()) + ")");
    check (d.getRejected() >= 25, "spikes and bursts: counted as rejected (" + std::to_string (d.getRejected()) + ")");

    // Impulsive noise: 40 spikes a second of 1-3 samples and up to 2 mV, both signs
    auto y = makeSignal (n, {}, {});
    std::mt19937 rng (11);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    for (int64_t i = 0; i < n - 4; i++)
        if (u (rng) < 40.0 / FS)
        {
            const int len = 1 + (int) (u (rng) * 3);
            const float a = (float) ((u (rng) * 4000.0) - 2000.0);
            for (int k = 0; k < len; k++)
                y[(size_t) (i + k)] += a;
        }
    const auto r = run (y, defaults(), 1024);
    check (r.onsets.empty(), "impulsive noise: no detections (" + std::to_string (r.onsets.size()) + ")");

    // The same impulsive noise with 1 ms pulses on top: every pulse still found once
    const auto pulses = pulseTrain (n, 1000.0, 30);
    auto z = makeSignal (n, pulses, {});
    const auto plain = makeSignal (n, {}, {}); // y without its spikes
    for (size_t i = 0; i < z.size(); i++)
        z[i] += y[i] - plain[i];
    const auto r2 = run (z, defaults(), 1024);
    check (r2.onsets.size() == pulses.size(), "impulsive noise with pulses: one onset per pulse (" + std::to_string (r2.onsets.size()) + " vs " + std::to_string (pulses.size()) + ")");
}

void testNoiseJump()
{
    std::printf ("Noise that rises during the recording\n");
    const int64_t n = (int64_t) (40 * FS);
    Options low;
    low.noiseUv = 10.0;
    Options high = low;
    high.noiseUv = 60.0;
    high.seed = 9;

    // 10 uV for 10 s, then 60 uV: no false detections at the change or after
    auto x = makeSignal (n, {}, low);
    const auto y = makeSignal (n, {}, high);
    for (int64_t i = (int64_t) (10 * FS); i < n; i++)
        x[(size_t) i] = y[(size_t) i];
    const auto r = run (x, defaults(), 1024);
    check (r.onsets.empty(), "noise 10 -> 60 uV: no false detections (" + std::to_string (r.onsets.size()) + ")");

    // And 60 -> 10 uV: pulses of 300 uV are found once the estimate has come down
    auto z = makeSignal (n, {}, high);
    std::vector<Pulse> pulses;
    for (double s = 15.0; s < 39.0; s += 2.0)
        pulses.push_back ({ (int64_t) (s * FS), 30, 300.0 });
    const auto w = makeSignal (n, pulses, low);
    for (int64_t i = (int64_t) (10 * FS); i < n; i++)
        z[(size_t) i] = w[(size_t) i];
    checkOnePerPulse ("noise 60 -> 10 uV, 300 uV pulses from 5 s after", run (z, defaults(), 1024), pulses, 2);
}

void testFloorWarmupAndOff()
{
    std::printf ("Min Step, warm-up and detection off\n");
    const int64_t n = (int64_t) (20 * FS);
    Options clean;
    clean.noiseUv = 1.0;
    clean.humUv = 0.0;

    // 50 uV steps on a clean signal are 50 SD, but under the 100 uV Min Step
    const auto steps = pulseTrain (n, 50.0, 30);
    check (run (makeSignal (n, steps, clean), defaults(), 1024).onsets.empty(), "Min Step: 50 uV steps ignored at 100 uV");
    auto noFloor = defaults();
    noFloor.minStepUv = 0.0;
    checkOnePerPulse ("Min Step 0: 50 uV steps on 1 uV noise", run (makeSignal (n, steps, clean), noFloor, 1024), steps, 1);

    // Nothing in the first 200 ms, while the noise is first estimated
    const std::vector<Pulse> early = { { (int64_t) (0.1 * FS), 30, 1000.0 }, { (int64_t) (2.0 * FS), 30, 1000.0 } };
    const auto r = run (makeSignal (n, early, {}), defaults(), 1024);
    check (r.onsets.size() == 1 && std::llabs (r.onsets[0].edge - early[1].start) <= 1, "warm-up: a pulse at 100 ms is skipped, the next found");

    // Both thresholds 0: detection off
    auto off = defaults();
    off.thresholdSds = 0.0;
    off.minStepUv = 0.0;
    check (run (makeSignal (n, pulseTrain (n, 1000.0, 300), {}), off, 1024).onsets.empty(), "thresholds 0: detection off");
}

void testAcCoupledWithArtefact()
{
    std::printf ("AC-coupled source (20 Hz high-pass) with a stimulation artefact 20 ms after each pulse\n");
    const int64_t n = (int64_t) (40 * FS);
    const auto pulses = pulseTrain (n, 1000.0, 300);

    Options o;
    o.highPassHz = 20.0;
    o.artefactUv = 1500.0; // larger than the pulse itself
    const auto r = run (makeSignal (n, pulses, o), defaults(), 1024);

    checkOnePerPulse ("AC + artefact", r, pulses, 1);
    std::printf ("  %zu pulses, %zu onsets (undershoot, falling edge and artefact all ignored)\n", pulses.size(), r.onsets.size());
}

void testLockout()
{
    std::printf ("Lockout\n");
    const int64_t n = (int64_t) (10 * FS);

    // Pulses at 1 s, 1.5 s (inside the 1 s lockout) and 2.2 s (after it)
    const std::vector<Pulse> pulses = { { (int64_t) (1.0 * FS), 300, 1000.0 },
                                        { (int64_t) (1.5 * FS), 300, 1000.0 },
                                        { (int64_t) (2.2 * FS), 300, 1000.0 } };
    const auto r = run (makeSignal (n, pulses, {}), defaults(), 512);

    check (r.onsets.size() == 2, "lockout: the pulse 0.5 s after an edge is ignored");
    if (r.onsets.size() == 2)
    {
        check (std::llabs (r.onsets[0].edge - pulses[0].start) <= 1, "lockout: first pulse detected");
        check (std::llabs (r.onsets[1].edge - pulses[2].start) <= 1, "lockout: pulse after the lockout detected");
    }

    auto shorter = defaults();
    shorter.lockoutMs = 300.0;
    const auto r2 = run (makeSignal (n, pulses, {}), shorter, 512);
    check (r2.onsets.size() == 3, "lockout: with 300 ms, all three pulses are detected");
}

void testLongStep()
{
    std::printf ("Step longer than the lockout\n");
    const int64_t n = (int64_t) (8 * FS);
    const std::vector<Pulse> pulses = { { (int64_t) (1.0 * FS), (int64_t) (3.0 * FS), 1000.0 } }; // 3 s high
    const auto r = run (makeSignal (n, pulses, {}), defaults(), 1024);

    // Not again when the lockout ends at 2 s with the step still high, and not at the falling edge at 4 s
    check (r.onsets.size() == 1, "long step: its rising edge only (" + std::to_string (r.onsets.size()) + ")");
    if (r.onsets.size() == 1)
        check (std::llabs (r.onsets[0].edge - pulses[0].start) <= 1, "long step: rising edge");
}

void testBlockSizes()
{
    std::printf ("Block size invariance\n");
    const int64_t n = (int64_t) (30 * FS);
    const auto pulses = pulseTrain (n, 800.0, 30);
    Options o;
    o.highPassHz = 20.0;
    o.noiseUv = 30.0;
    const auto x = makeSignal (n, pulses, o);

    const auto a = run (x, defaults(), 1024);
    check (a.onsets.size() == pulses.size(), "blocks: every pulse in the reference run");
    for (int block : { 1, 7, 128, 333, 4096 })
    {
        const auto b = run (x, defaults(), block);
        bool same = a.onsets.size() == b.onsets.size() && a.offsets == b.offsets;
        for (size_t k = 0; same && k < a.onsets.size(); k++)
            same = a.onsets[k].decision == b.onsets[k].decision && a.onsets[k].edge == b.onsets[k].edge;
        check (same, "blocks of " + std::to_string (block) + " give identical edges");
    }
}

void testEdgeAtBlockBoundary()
{
    std::printf ("Edge straddling a block boundary\n");
    const int64_t n = (int64_t) (3 * FS);
    // The pulse starts on the last sample of a 1024-sample block
    const std::vector<Pulse> pulses = { { 1024 * 30 - 1, 300, 1000.0 } };
    Options quiet;
    quiet.noiseUv = 1.0;
    quiet.humUv = 0.0;
    const auto r = run (makeSignal (n, pulses, quiet), defaults(), 1024);
    check (r.onsets.size() == 1 && r.onsets[0].edge == pulses[0].start && r.onsets[0].decision == pulses[0].start + confirmSamples() - 1,
           "boundary: edge on the previous block's last sample, decision Min Width later in the next block");
}
void testExternalOnsets()
{
    std::printf ("External onsets (pulse edges from a TTL line)\n");
    const int64_t n = (int64_t) (40 * FS);
    const auto pulses = pulseTrain (n, 1000.0, 30);
    std::vector<int64_t> edges;
    for (const auto& p : pulses)
        edges.push_back (p.start);

    // File Reader case: the TTL edges only, nothing on the channel
    {
        const auto x = makeSignal (n, {}, {});
        const auto r = runExternal (x, defaults(), 1024, edges);
        bool exact = r.onsets.size() == edges.size();
        for (size_t k = 0; exact && k < edges.size(); k++)
            exact = r.onsets[k].edge == edges[k] && r.onsets[k].decision == edges[k];
        check (exact, "TTL only: one onset exactly at every edge (" + std::to_string (r.onsets.size()) + ")");
        bool widths = r.offsets.size() == r.onsets.size();
        for (size_t k = 0; widths && k < r.offsets.size(); k++)
            widths = r.offsets[k] - r.onsets[k].decision == 300;
        check (widths, "TTL only: 10 ms output pulses");
    }

    // The channel's own crossings are not used: pulses on the channel, no TTL edges
    {
        const auto r = runExternal (makeSignal (n, pulses, {}), defaults(), 1024, {});
        check (r.onsets.empty(), "external mode: pulses on the channel alone are not detected (" + std::to_string (r.onsets.size()) + ")");
    }

    // Edges during the lockout are ignored and counted
    {
        std::vector<int64_t> dense = { (int64_t) (1.0 * FS), (int64_t) (1.4 * FS), (int64_t) (2.2 * FS), (int64_t) (2.5 * FS) };
        uint32_t ignored = 0;
        const auto r = runExternal (makeSignal ((int64_t) (4 * FS), {}, {}), defaults(), 1024, dense, &ignored);
        check (r.onsets.size() == 2 && r.onsets[0].edge == dense[0] && r.onsets[1].edge == dense[2],
               "external lockout: edges 0.4 s and 0.3 s after a pulse are ignored");
        check (ignored == 2, "external lockout: ignored edges counted (" + std::to_string (ignored) + ")");
    }

    // Periodic use: onsets every 0.5 s with a lockout of 0.45 s are all accepted
    {
        std::vector<int64_t> ticks;
        for (int k = 1; k <= 8; k++)
            ticks.push_back ((int64_t) (k * 0.5 * FS));
        auto p = defaults();
        p.lockoutMs = 450.0;
        uint32_t ignored = 0;
        const auto r = runExternal (makeSignal ((int64_t) (5 * FS), {}, {}), p, 777, ticks, &ignored);
        bool exact = r.onsets.size() == ticks.size() && ignored == 0;
        for (size_t k = 0; exact && k < ticks.size(); k++)
            exact = r.onsets[k].edge == ticks[k];
        check (exact, "periodic onsets 0.5 s apart, lockout 0.45 s: every onset exactly on time");
    }
}
} // namespace

int main()
{
    testRisingOnly();
    testNoiseLevels();
    testMixedAmplitudes();
    testSpikesAndBursts();
    testNoiseJump();
    testFloorWarmupAndOff();
    testAcCoupledWithArtefact();
    testLockout();
    testLongStep();
    testBlockSizes();
    testEdgeAtBlockBoundary();
    testExternalOnsets();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
