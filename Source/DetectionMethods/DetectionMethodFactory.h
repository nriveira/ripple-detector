#ifndef __DETECTION_METHOD_FACTORY_H
#define __DETECTION_METHOD_FACTORY_H

#include "EnvelopeMethod.h"
#include "RmsWindowMethod.h"
#include "TkeoMethod.h"

/**
    Maps a method name to a DetectionMethod instance.

    The plugin currently exposes only the RMS method. The Envelope and TKEO
    implementations are kept (and unit-tested) for future use; to expose one, add its
    name to getDetectionMethodNames() and reintroduce a "method" parameter.
*/
inline Array<String> getDetectionMethodNames()
{
    return { "RMS" };
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
