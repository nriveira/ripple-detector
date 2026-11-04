#ifndef __DETECTION_METHOD_FACTORY_H
#define __DETECTION_METHOD_FACTORY_H

#include "EnvelopeMethod.h"
#include "RmsWindowMethod.h"
#include "TkeoMethod.h"

/**
    Maps a method name to a DetectionMethod instance.

    The names are the first entries of the Detect drop-down, in this order (the
    position is what a configuration saves).
*/
constexpr int NUM_DETECTION_METHODS = 3;

inline Array<String> getDetectionMethodNames()
{
    return { "RMS", "Envelope", "TKEO" };
}

inline std::unique_ptr<DetectionMethod> createDetectionMethod (const String& name)
{
    if (name.equalsIgnoreCase ("Envelope"))
        return std::make_unique<EnvelopeMethod>();

    if (name.equalsIgnoreCase ("TKEO"))
        return std::make_unique<TkeoMethod>();

    return std::make_unique<RmsWindowMethod>();
}

#endif
