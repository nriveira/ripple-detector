/*
    Standalone tests for the debug pulse-edge detector (Source/DetectionMethods/PulseEdgeDetector).

    The signals mimic test pulses picked up in saline: Gaussian noise and mains hum, square
    pulses of either polarity, optionally through an AC coupling (high-pass) like a sound
    card output, and a short artefact from the stimulation that follows each pulse.
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
    unsigned seed = 7;
};

std::vector<float> makeSignal (int64_t n, const std::vector<Pulse>& pulses, const Options& o)
{
    std::vector<double> source ((size_t) n, 0.0);
    for (const auto& p : pulses)
    {
        for (int64_t i = p.start; i < std::min (n, p.start + p.length); i++)
            source[(size_t) i] += p.amplitude;
        if (o.artefactUv > 0.0)
            for (int64_t i = p.start + o.artefactDelay; i < std::min (n, p.start + o.artefactDelay + 60); i++)
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
            if (e.state)
                r.onsets.push_back ({ pos + e.sampleIndex, pos + e.sampleIndex - e.edgeOffset });
            else
                r.offsets.push_back (pos + e.sampleIndex);
        }
    }
    return r;
}

PulseEdgeDetector::Params defaults()
{
    PulseEdgeDetector::Params p;
    p.sampleRate = FS;
    p.thresholdUv = 200.0;
    p.lockoutMs = 1000.0;
    p.ttlMs = 10.0;
    return p;
}

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

void testPolarities()
{
    std::printf ("Square pulses, both polarities\n");
    const int64_t n = (int64_t) (40 * FS);

    for (double amp : { 1000.0, -1000.0 })
    {
        const auto pulses = pulseTrain (n, amp, 300); // 10 ms pulses
        const auto r = run (makeSignal (n, pulses, {}), defaults(), 1024);
        const std::string name = amp > 0 ? "positive" : "negative";
        checkOnePerPulse (name, r, pulses, 1);
        check (r.offsets.size() == r.onsets.size(), name + ": every TTL pulse ends");
        for (size_t k = 0; k < std::min (r.onsets.size(), r.offsets.size()); k++)
            check (r.offsets[k] - r.onsets[k].decision == 300, name + ": TTL lasts 10 ms");
        std::printf ("  %-8s %zu pulses, %zu onsets\n", name.c_str(), pulses.size(), r.onsets.size());
    }
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
    std::printf ("  %zu pulses, %zu onsets (undershoot, falling edge and artefact all inside the lockout)\n", pulses.size(), r.onsets.size());
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

    // Not again when the lockout ends at 2 s with the step still high; the falling
    // edge at 4 s is a new edge, since the lockout is shorter than the step
    check (r.onsets.size() == 2, "long step: rising and falling edge only (" + std::to_string (r.onsets.size()) + ")");
    if (r.onsets.size() == 2)
    {
        check (std::llabs (r.onsets[0].edge - pulses[0].start) <= 1, "long step: rising edge");
        check (std::llabs (r.onsets[1].edge - (pulses[0].start + pulses[0].length)) <= 1, "long step: falling edge, not the end of the lockout");
    }
}

void testGlitchesAndNoise()
{
    std::printf ("Glitches and noise\n");
    const int64_t n = (int64_t) (20 * FS);

    // Single-sample spikes well above threshold, and no pulses
    auto x = makeSignal (n, {}, {});
    for (int64_t t = (int64_t) FS; t < n; t += (int64_t) (0.7 * FS))
        x[(size_t) t] += 3000.0f;
    const auto r = run (x, defaults(), 1024);
    check (r.onsets.empty(), "glitches: single-sample spikes are ignored (" + std::to_string (r.onsets.size()) + ")");

    // Noise alone, with a threshold 6x its SD: nothing
    Options noisy;
    noisy.noiseUv = 33.0;
    noisy.humUv = 0.0;
    const auto r2 = run (makeSignal (n, {}, noisy), defaults(), 1024);
    check (r2.onsets.empty(), "noise: no detections at 6 SD (" + std::to_string (r2.onsets.size()) + ")");

    // A threshold of 0 disables detection
    auto off = defaults();
    off.thresholdUv = 0.0;
    const auto r3 = run (makeSignal (n, pulseTrain (n, 1000.0, 300), {}), off, 1024);
    check (r3.onsets.empty(), "threshold 0: detection off");
}

void testBlockSizes()
{
    std::printf ("Block size invariance\n");
    const int64_t n = (int64_t) (30 * FS);
    const auto pulses = pulseTrain (n, -800.0, 300);
    Options o;
    o.highPassHz = 20.0;
    const auto x = makeSignal (n, pulses, o);

    const auto a = run (x, defaults(), 1024);
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
    check (r.onsets.size() == 1 && r.onsets[0].edge == pulses[0].start && r.onsets[0].decision == pulses[0].start + 1,
           "boundary: edge on the previous block's last sample, decision on the next block's first");
}
} // namespace

int main()
{
    testPolarities();
    testAcCoupledWithArtefact();
    testLockout();
    testLongStep();
    testGlitchesAndNoise();
    testBlockSizes();
    testEdgeAtBlockBoundary();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
