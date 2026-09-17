/*
    Standalone tests for the ripple detection methods.

    The detection layer (Source/DetectionMethods) has no JUCE dependency, so this
    file can be compiled with any C++17 compiler:

        c++ -std=c++17 -O2 -I ../Source DetectionMethodTests.cpp ../Source/DetectionMethods/<each .cpp> -o detection_tests
        ./detection_tests

    or via the CMakeLists.txt in this directory.

    The synthetic signal mimics an already band-pass filtered (150-250 Hz) recording:
    band-limited Gaussian noise with ripple bursts, short spike artefacts and, for the
    adaptive baseline test, a slow drift in noise amplitude.
*/

#include "DetectionMethods/EnvelopeMethod.h"
#include "DetectionMethods/RmsWindowMethod.h"
#include "DetectionMethods/TkeoMethod.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr float SAMPLE_RATE = 30000.0f;
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

/** Second-order band-pass biquad (RBJ cookbook) used to band-limit the synthetic noise */
struct BandPass
{
    BandPass (double f0, double q, double fs)
    {
        const double w0 = 2.0 * PI * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b1 = 0.0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double operator() (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    double b0, b1, b2, a1, a2;
    double x1 { 0 }, x2 { 0 }, y1 { 0 }, y2 { 0 };
};

struct Burst
{
    int start; // sample
    int length; // samples
};

struct Signal
{
    std::vector<float> data;
    std::vector<Burst> ripples;
    std::vector<int> artefacts;
};

/**
    Builds a test signal: 'calibrationSeconds' of clean noise followed by ripples
    every 'intervalSeconds'. Ripples are 200 Hz bursts with a Hann envelope.
*/
Signal makeSignal (double totalSeconds,
                   double calibrationSeconds,
                   double intervalSeconds,
                   double rippleAmplitude,
                   double rippleMs = 50.0,
                   bool addArtefacts = true,
                   std::function<double (double)> noiseGain = nullptr,
                   unsigned seed = 42)
{
    Signal s;
    const int n = (int) (totalSeconds * SAMPLE_RATE);
    s.data.assign ((size_t) n, 0.0f);

    std::mt19937 rng (seed);
    std::normal_distribution<double> noise (0.0, 1.0);
    BandPass bp (200.0, 2.0, SAMPLE_RATE);

    // Band-limited noise; the filter gain leaves an RMS of roughly 0.2
    for (int i = 0; i < n; i++)
    {
        const double t = i / (double) SAMPLE_RATE;
        const double gain = noiseGain ? noiseGain (t) : 1.0;
        s.data[(size_t) i] = (float) (gain * bp (noise (rng)));
    }

    const int rippleLen = (int) (rippleMs * SAMPLE_RATE / 1000.0);
    const int interval = (int) (intervalSeconds * SAMPLE_RATE);
    int eventIndex = 0;

    for (int start = (int) (calibrationSeconds * SAMPLE_RATE) + interval / 2; start + rippleLen < n; start += interval)
    {
        if (addArtefacts && (eventIndex % 3 == 2))
        {
            // Spike artefact: 1 ms band-passed transient, large amplitude
            BandPass spike (200.0, 2.0, SAMPLE_RATE);
            for (int i = 0; i < 300 && start + i < n; i++)
                s.data[(size_t) (start + i)] += (float) (spike (i == 0 ? 40.0 * rippleAmplitude : 0.0));
            s.artefacts.push_back (start);
        }
        else
        {
            for (int i = 0; i < rippleLen; i++)
            {
                const double hann = 0.5 * (1.0 - std::cos (2.0 * PI * i / (rippleLen - 1)));
                s.data[(size_t) (start + i)] += (float) (rippleAmplitude * hann * std::sin (2.0 * PI * 200.0 * i / SAMPLE_RATE));
            }
            s.ripples.push_back ({ start, rippleLen });
        }
        eventIndex++;
    }

    return s;
}

struct AbsEvent
{
    int64_t sample;
    bool state;
};

/** Runs calibration and detection over a signal in fixed-size blocks, returning absolute events */
std::vector<AbsEvent> run (DetectionMethod& method,
                           const Signal& s,
                           double calibrationSeconds,
                           int blockSize)
{
    std::vector<AbsEvent> out;
    std::vector<DetectionEvent> events;

    const int64_t calibrationSamples = (int64_t) (calibrationSeconds * SAMPLE_RATE);
    const int64_t n = (int64_t) s.data.size();

    method.reset();

    int64_t pos = 0;
    bool calibrating = true;

    while (pos < n)
    {
        const int len = (int) std::min<int64_t> (blockSize, n - pos);
        const float* block = s.data.data() + pos;

        if (calibrating)
        {
            method.calibrate (block, len);
            if (pos + len >= calibrationSamples)
            {
                method.finishCalibration();
                calibrating = false;
            }
        }
        else
        {
            events.clear();
            method.process (block, len, events);
            for (const auto& e : events)
                out.push_back ({ pos + e.sampleIndex, e.state });
        }

        pos += len;
    }

    return out;
}

std::vector<int64_t> onsets (const std::vector<AbsEvent>& events)
{
    std::vector<int64_t> r;
    for (const auto& e : events)
        if (e.state)
            r.push_back (e.sample);
    return r;
}

DetectionParams defaultParams()
{
    DetectionParams p;
    p.sampleRate = SAMPLE_RATE;
    p.onsetSds = 5.0;
    p.offsetSds = 2.0;
    p.minDurationMs = 10;
    p.refractoryMs = 140;
    p.rmsSamples = 128;
    p.smoothingMs = 5.0;
    return p;
}

std::unique_ptr<DetectionMethod> makeMethod (const std::string& name)
{
    if (name == "Envelope")
        return std::make_unique<EnvelopeMethod>();
    if (name == "TKEO")
        return std::make_unique<TkeoMethod>();
    return std::make_unique<RmsWindowMethod>();
}

const std::vector<std::string> METHODS = { "RMS", "Envelope", "TKEO" };

// ---------------------------------------------------------------------------

void testDetectsRipplesAndRejectsArtefacts()
{
    std::printf ("Ripple detection / artefact rejection\n");

    const double calib = 20.0;
    Signal s = makeSignal (60.0, calib, 1.5, 1.5);

    for (const auto& name : METHODS)
    {
        auto method = makeMethod (name);
        method->setParams (defaultParams());

        auto events = run (*method, s, calib, 1024);
        auto ons = onsets (events);

        std::printf ("  %-8s baseline mean %.4g, std %.4g, %zu ripples, %zu artefacts, %zu onsets\n",
                     name.c_str(),
                     method->getBaselineMean(),
                     method->getBaselineStd(),
                     s.ripples.size(),
                     s.artefacts.size(),
                     ons.size());

        check (ons.size() == s.ripples.size(), name + ": one onset per ripple");

        // Every onset falls inside a ripple (allowing the smoothing / window latency)
        size_t hits = 0;
        for (auto on : ons)
            for (const auto& r : s.ripples)
                if (on >= r.start && on <= r.start + r.length + 300)
                    hits++;
        check (hits == ons.size(), name + ": every onset lies within a ripple");

        // No onset near a spike artefact
        for (auto on : ons)
            for (auto a : s.artefacts)
                check (std::llabs (on - a) > 3000, name + ": onset near spike artefact");

        // Detection latency from ripple start
        int64_t maxLatency = 0;
        for (auto on : ons)
            for (const auto& r : s.ripples)
                if (on >= r.start && on <= r.start + r.length + 300)
                    maxLatency = std::max<int64_t> (maxLatency, on - r.start);
        std::printf ("           max onset latency %.1f ms\n", 1000.0 * maxLatency / SAMPLE_RATE);

        // TTL edges alternate and end low
        bool high = false;
        bool alternates = true;
        for (const auto& e : events)
        {
            if (e.state == high)
                alternates = false;
            high = e.state;
        }
        check (alternates, name + ": TTL edges alternate");
        check (! method->isEventActive(), name + ": no event left open");
    }
}

void testBlockSizeInvariance()
{
    std::printf ("Block size invariance\n");

    const double calib = 20.0;
    Signal s = makeSignal (40.0, calib, 1.5, 1.5);

    for (const auto& name : METHODS)
    {
        // RMS windows are aligned to the block, so use block sizes that are multiples of rmsSamples
        const int blockA = 1024;
        const int blockB = (name == "RMS") ? 512 : 333;

        auto m1 = makeMethod (name);
        m1->setParams (defaultParams());
        auto e1 = run (*m1, s, calib, blockA);

        auto m2 = makeMethod (name);
        m2->setParams (defaultParams());
        auto e2 = run (*m2, s, calib, blockB);

        bool same = e1.size() == e2.size();
        for (size_t i = 0; same && i < e1.size(); i++)
            same = e1[i].sample == e2[i].sample && e1[i].state == e2[i].state;

        // The RMS calibration windows differ slightly between block sizes, so allow
        // the per-sample methods to be exact and the RMS method to match in count
        if (name == "RMS")
            check (e1.size() == e2.size() && e1.size() > 0, name + ": same number of events for different block sizes");
        else
            check (same && ! e1.empty(), name + ": identical events for different block sizes");
    }
}

void testRefractory()
{
    std::printf ("Refractory period\n");

    // Ripple pairs 60 ms apart, refractory 140 ms: only the first of each pair may fire
    const double calib = 20.0;
    Signal s = makeSignal (40.0, calib, 1.5, 1.5, 30.0, false);
    Signal pairs = s;
    for (const auto& r : s.ripples)
    {
        const int start2 = r.start + (int) (0.06 * SAMPLE_RATE);
        for (int i = 0; i < r.length; i++)
        {
            const double hann = 0.5 * (1.0 - std::cos (2.0 * PI * i / (r.length - 1)));
            pairs.data[(size_t) (start2 + i)] += (float) (1.5 * hann * std::sin (2.0 * PI * 200.0 * i / SAMPLE_RATE));
        }
    }

    for (const auto& name : METHODS)
    {
        auto p = defaultParams();

        auto withRefractory = makeMethod (name);
        withRefractory->setParams (p);
        auto ons1 = onsets (run (*withRefractory, pairs, calib, 1024));

        p.refractoryMs = 0;
        auto noRefractory = makeMethod (name);
        noRefractory->setParams (p);
        auto ons2 = onsets (run (*noRefractory, pairs, calib, 1024));

        // Count second bursts that received at least one onset (the RMS method re-fires
        // every window once the duration is reached, so it can emit several pulses per burst)
        auto secondBurstsHit = [&] (const std::vector<int64_t>& ons)
        {
            size_t hit = 0;
            for (const auto& r : s.ripples)
            {
                const int64_t start2 = r.start + (int64_t) (0.06 * SAMPLE_RATE);
                bool found = false;
                for (auto on : ons)
                    found = found || (on >= start2 && on <= start2 + r.length + 300);
                hit += found ? 1 : 0;
            }
            return hit;
        };

        std::printf ("  %-8s %zu onsets with 140 ms refractory, %zu without\n", name.c_str(), ons1.size(), ons2.size());
        check (ons1.size() == s.ripples.size(), name + ": second ripple of each pair suppressed by refractory");
        check (secondBurstsHit (ons1) == 0, name + ": no onset in the second burst with refractory");
        check (secondBurstsHit (ons2) == s.ripples.size(), name + ": every second burst detected without refractory");
    }
}

void testHysteresisAndMaxDuration()
{
    std::printf ("Hysteresis offset / max duration\n");

    const double calib = 20.0;
    Signal s = makeSignal (40.0, calib, 1.5, 1.5, 80.0, false);

    for (const auto& name : { std::string ("Envelope"), std::string ("TKEO") })
    {
        auto p = defaultParams();
        auto method = makeMethod (name);
        method->setParams (p);
        auto events = run (*method, s, calib, 1024);

        // Events should last a good fraction of the 80 ms burst (offset at 2 SD)
        double meanDurationMs = 0.0;
        int count = 0;
        for (size_t i = 0; i + 1 < events.size(); i++)
        {
            if (events[i].state && ! events[i + 1].state)
            {
                meanDurationMs += 1000.0 * (events[i + 1].sample - events[i].sample) / SAMPLE_RATE;
                count++;
            }
        }
        meanDurationMs /= std::max (1, count);
        std::printf ("  %-8s mean event duration %.1f ms\n", name.c_str(), meanDurationMs);
        check (meanDurationMs > 20.0 && meanDurationMs < 80.0, name + ": event duration follows the burst envelope");

        // With a 15 ms max duration every event must end within 15 ms of onset
        p.maxDurationMs = 15;
        auto capped = makeMethod (name);
        capped->setParams (p);
        auto cappedEvents = run (*capped, s, calib, 1024);

        bool allCapped = ! cappedEvents.empty();
        for (size_t i = 0; i + 1 < cappedEvents.size(); i++)
            if (cappedEvents[i].state && ! cappedEvents[i + 1].state)
                allCapped = allCapped && (cappedEvents[i + 1].sample - cappedEvents[i].sample) <= (int64_t) (0.015 * SAMPLE_RATE) + 1;
        check (allCapped, name + ": max duration caps every event");
    }
}

void testAdaptiveBaseline()
{
    std::printf ("Adaptive baseline under drift\n");

    // Noise amplitude ramps from 1x to 3x between 20 s and 80 s; no ripples at all
    const double calib = 20.0;
    auto ramp = [] (double t)
    { return t < 20.0 ? 1.0 : 1.0 + 2.0 * std::min (1.0, (t - 20.0) / 60.0); };
    Signal s = makeSignal (100.0, calib, 1e9, 0.0, 50.0, false, ramp);

    for (const auto& name : METHODS)
    {
        auto p = defaultParams();

        auto fixed = makeMethod (name);
        fixed->setParams (p);
        auto fixedOns = onsets (run (*fixed, s, calib, 1024));

        p.adaptiveBaseline = true;
        p.adaptTauSeconds = 5.0;
        auto adaptive = makeMethod (name);
        adaptive->setParams (p);
        auto adaptiveOns = onsets (run (*adaptive, s, calib, 1024));

        std::printf ("  %-8s false positives: fixed %zu, adaptive %zu (baseline mean %.3g -> %.3g)\n",
                     name.c_str(),
                     fixedOns.size(),
                     adaptiveOns.size(),
                     fixed->getBaselineMean(),
                     adaptive->getBaselineMean());

        check (fixedOns.size() > 10, name + ": fixed baseline produces false positives under drift (sanity)");
        check (adaptiveOns.size() * 5 < fixedOns.size(), name + ": adaptive baseline suppresses drift false positives");
        check (adaptive->getBaselineMean() > 2.0 * fixed->getBaselineMean(), name + ": adaptive baseline tracked the ramp");
    }
}

void testParameterChangeMidRun()
{
    std::printf ("Parameter change while running\n");

    // Raising the onset threshold far above the ripple amplitude stops detections
    const double calib = 20.0;
    Signal s = makeSignal (40.0, calib, 1.5, 1.5);

    for (const auto& name : METHODS)
    {
        auto p = defaultParams();
        auto method = makeMethod (name);
        method->setParams (p);

        std::vector<DetectionEvent> events;
        int64_t onsetsBefore = 0, onsetsAfter = 0;
        const int64_t calibSamples = (int64_t) (calib * SAMPLE_RATE);
        const int64_t half = (int64_t) (30.0 * SAMPLE_RATE);
        const int block = 1024;

        method->reset();
        for (int64_t pos = 0; pos < (int64_t) s.data.size(); pos += block)
        {
            const int len = (int) std::min<int64_t> (block, (int64_t) s.data.size() - pos);
            if (pos < calibSamples)
            {
                method->calibrate (s.data.data() + pos, len);
                if (pos + len >= calibSamples)
                    method->finishCalibration();
                continue;
            }

            if (pos >= half && p.onsetSds < 1000.0)
            {
                p.onsetSds = 1000.0;
                method->setParams (p);
            }

            events.clear();
            method->process (s.data.data() + pos, len, events);
            for (const auto& e : events)
                if (e.state)
                    (pos < half ? onsetsBefore : onsetsAfter)++;
        }

        std::printf ("  %-8s onsets before %lld, after threshold raised %lld\n", name.c_str(), (long long) onsetsBefore, (long long) onsetsAfter);
        check (onsetsBefore > 0, name + ": detections before parameter change");
        check (onsetsAfter == 0, name + ": raised threshold applied immediately");
    }
}
void testFeatureOutput()
{
    std::printf ("Feature output\n");

    const double calib = 20.0;
    Signal s = makeSignal (30.0, calib, 1.5, 1.5, 50.0, false);

    for (const auto& name : METHODS)
    {
        auto p = defaultParams();
        auto method = makeMethod (name);
        method->setParams (p);
        method->reset();

        const int block = 1024;
        const int64_t calibSamples = (int64_t) (calib * SAMPLE_RATE);
        std::vector<float> feature ((size_t) s.data.size(), -1.0f);
        std::vector<DetectionEvent> events;
        std::vector<int64_t> ons;

        for (int64_t pos = 0; pos < (int64_t) s.data.size(); pos += block)
        {
            const int len = (int) std::min<int64_t> (block, (int64_t) s.data.size() - pos);
            float* out = feature.data() + pos;

            if (pos < calibSamples)
            {
                method->calibrate (s.data.data() + pos, len, out);
                if (pos + len >= calibSamples)
                    method->finishCalibration();
                continue;
            }

            events.clear();
            method->process (s.data.data() + pos, len, events, out);
            for (const auto& e : events)
                if (e.state)
                    ons.push_back (pos + e.sampleIndex);
        }

        // Every sample was written, and the feature is finite
        bool allWritten = true;
        for (float v : feature)
            allWritten = allWritten && v != -1.0f && std::isfinite (v);
        check (allWritten, name + ": feature written for every sample (calibration and detection)");

        // At each onset the feature is above the onset threshold
        bool aboveAtOnset = ! ons.empty();
        for (auto on : ons)
            aboveAtOnset = aboveAtOnset && feature[(size_t) on] > (float) method->getOnsetThreshold();
        check (aboveAtOnset, name + ": feature exceeds onset threshold at every onset");

        // The calibration statistics match the emitted feature
        double sum = 0.0;
        for (int64_t i = 0; i < calibSamples; i++)
            sum += feature[(size_t) i];
        const double meanOfOutput = sum / (double) calibSamples;
        const double rel = std::fabs (meanOfOutput - method->getBaselineMean()) / std::max (1e-12, std::fabs (method->getBaselineMean()));
        check (rel < 0.02, name + ": baseline mean matches the emitted feature during calibration");

        std::printf ("  %-8s feature mean during calibration %.4g (baseline %.4g)\n", name.c_str(), meanOfOutput, method->getBaselineMean());
    }
}
} // namespace

int main()
{
    testDetectsRipplesAndRejectsArtefacts();
    testBlockSizeInvariance();
    testRefractory();
    testHysteresisAndMaxDuration();
    testAdaptiveBaseline();
    testParameterChangeMidRun();
    testFeatureOutput();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
