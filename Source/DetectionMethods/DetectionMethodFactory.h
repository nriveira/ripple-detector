#ifndef __DETECTION_METHOD_FACTORY_H
#define __DETECTION_METHOD_FACTORY_H

#include "EnvelopeMethod.h"
#include "RmsWindowMethod.h"
#include "TkeoMethod.h"

/**
    Maps the "method" parameter to a DetectionMethod instance.

    To add a method: implement a DetectionMethod subclass, add its name to
    getDetectionMethodNames() and construct it in createDetectionMethod(). The names
    must match the categories of the "method" parameter in RippleDetector::registerParameters().
*/
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
