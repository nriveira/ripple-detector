#include "RippleDetector.h"
#include "DetectionMethods/DetectionMethodFactory.h"
#include "RippleDetectorEditor.h"

#define CALIBRATION_DURATION_SECONDS 20
#define VIEWER_FIFO_SECONDS 4.0f
#define LATENCY_TIMEOUT_SECONDS 0.5 // an onset's stimulus must arrive within this long
#define MAX_NOISE_CHOICES 1024 // channels a saved noise-channel choice can refer to (see registerParameters)

RippleDetectorSettings::RippleDetectorSettings()
{
}

TTLEventPtr RippleDetectorSettings::createEvent (int64 outputLine, int64 sample_number, bool state)
{
    return TTLEvent::createTTLEvent (
        eventChannel,
        sample_number,
        outputLine,
        state);
}

RippleDetector::RippleDetector() : GenericProcessor ("Ripple Detector")
{
}

void RippleDetector::registerParameters()
{
    /* Ripple Detection Settings */

    // The ripple and noise channels are drop-downs of the stream's channel names, so the choice
    // is made by name even when a Channel Map upstream has reordered the channels (a channel
    // selector shows positions). The placeholder lists are replaced by the channel names in
    // updateSettings(); they only need to be long enough for a saved choice to survive loading
    // a configuration. setChannelChoices() keeps each choice on the same channel name.
    {
        Array<String> choices;
        for (int i = 1; i <= MAX_NOISE_CHOICES; i++)
            choices.add (String (i));

        addCategoricalParameter (
            Parameter::STREAM_SCOPE,
            "ripple_channel",
            "Ripple Input",
            "Continuous input channel on which ripples are detected, by channel name (in the test modes it is only shown). \
Defaults to the stream's first channel.",
            choices,
            0);
    }

    // The noise channel's first entry is "None" (the default): a channel selector would pre-select
    // the stream's first channel and turn the veto on by default.
    {
        Array<String> choices { "None" };
        for (int i = 1; i <= MAX_NOISE_CHOICES; i++)
            choices.add (String (i));

        addCategoricalParameter (
            Parameter::STREAM_SCOPE,
            "noise_channel",
            "Noise Input",
            "Optional channel that should not carry ripples. None (default): detection uses the ripple channel alone. \
Otherwise the noise channel is judged with the same detection settings against its own baseline, and a ripple onset is \
suppressed (no TTL, no laser trigger) when the noise channel is above its threshold in the same RMS window.",
            choices,
            0);
    }

    addTtlLineParameter (
        Parameter::STREAM_SCOPE,
        "Ripple_Out",
        "Ripple Output",
        "TTL line on which output events will be triggered",
        16);

    addTtlLineParameter (
        Parameter::STREAM_SCOPE,
        "stim_in",
        "Stim. Input",
        "Digital input line on which the acquisition board records the stimulus controller's trigger. Each ripple onset is \
paired with the next rising edge on this line within 500 ms, and the difference is reported as the stimulation latency: \
everything from the samples reaching the computer to the controller's trigger (detection, GUI buffering, network, \
controller), excluding the headstage-to-board and stimulator latencies.",
        16,
        false,
        true);

    /* Test modes */
    addCategoricalParameter (
        Parameter::STREAM_SCOPE,
        "detect_mode",
        "Detect",
        "Ripple: detect sharp-wave ripples (normal use). Pulse (test): detect the leading edge of square test pulses on the \
Ripple Input channel (from a File Reader recording, or played into saline) and respond exactly as to a ripple (Ripple Output \
TTL, laser trigger, stimulation latency). Periodic (test): respond Pulses times, Interval seconds apart, ignoring the \
continuous signal, for repeated tests of the stimulation. The test modes bypass the noise veto and movement gating, and \
switching back to Ripple recalibrates the stream.",
        { "Ripple", "Pulse (test)", "Periodic (test)" },
        0);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "pulse_thresh",
        "Pulse Thresh.",
        "Pulse (test) mode: distance from the channel's running baseline, in the channel's units (uV for headstage channels), \
that counts as a pulse edge. Either polarity. Must hold for 2 consecutive samples.",
        "uV",
        200,
        0,
        1000000,
        10);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "pulse_lockout",
        "Lockout",
        "Pulse (test) mode: no new pulse is detected for this long after one, so the pulse's falling edge, the undershoot of \
an AC-coupled source and the stimulation's own artefact are not detected as pulses. Keep it longer than the test pulses and \
shorter than the gap between them.",
        "ms",
        1000,
        0,
        60000,
        10);

    addIntParameter (
        Parameter::STREAM_SCOPE,
        "periodic_count",
        "Pulses",
        "Periodic (test) mode: how many responses to send. The sequence starts one Interval after acquisition starts (or after \
switching to this mode).",
        10,
        1,
        100000);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "periodic_interval",
        "Interval",
        "Periodic (test) mode: time between responses.",
        "s",
        2,
        0.1,
        3600,
        0.1);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "ripple_std",
        "Onset Std Dev",
        "Number of baseline standard deviations above the mean that defines the detection (onset) threshold",
        "",
        5, //default
        0, //min
        9999, //max
        1 //step
    );

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "time_thresh",
        "Time Thresh.",
        "Minimum period (in ms) during which the signal must be above the onset threshold for a ripple to be detected.",
        "ms",
        10,
        0,
        9999,
        1);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "refr_time",
        "Refrac. Time",
        "The period (in ms) after each detection event in which new ripples cannot be detected.",
        "ms",
        140,
        0,
        999999,
        1);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "rms_samples",
        "RMS Samples",
        "Number of samples in each RMS window (used by the RMS method and by movement detection)",
        "",
        128,
        1,
        2048,
        1);

    /* Feature output */
    addBooleanParameter (
        Parameter::STREAM_SCOPE,
        "feature_out",
        "Feature Output",
        "Adds three continuous channels to the stream: the RMS feature (RIP_FEAT), the detection threshold \
(RIP_THR = baseline mean + Onset Std Dev x SD) and a pulse at threshold height while a ripple is detected (RIP_EVENT). \
Useful for the LFP Viewer and for recording; the built-in viewer does not need this.",
        false,
        true); // changing this rebuilds the signal chain, so lock it during acquisition

    /* Baseline settings */
    addCategoricalParameter (
        Parameter::STREAM_SCOPE,
        "baseline",
        "Baseline",
        "Fixed: baseline mean and SD are computed once during calibration. \
Adaptive: after calibration, the baseline keeps tracking the signal outside of detected events with the time constant below.",
        { "Fixed", "Adaptive" },
        0);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "adapt_tau",
        "Adapt Tau",
        "Time constant (in s) of the adaptive baseline",
        "s",
        60,
        1,
        99999,
        1);

    /* Closed-loop laser trigger (LaserDriver Pi: UDP trigger port, or the web API) */
    addBooleanParameter (
        Parameter::PROCESSOR_SCOPE,
        "laser_trigger",
        "Laser Trigger",
        "Fires the laser on every ripple onset through the LaserDriver Pi, from the same block as the TTL event.",
        false);

    addCategoricalParameter (
        Parameter::PROCESSOR_SCOPE,
        "laser_transport",
        "Laser Via",
        "UDP: a datagram to laserhat-brokerd's trigger port, sent directly from the processing thread and acknowledged \
with the Pi's own receive-to-edge time. HTTP: POST /api/trigger_gpio to the Pi's web GUI, for a Pi without the UDP listener.",
        { "UDP", "HTTP" },
        0,
        true);

    addStringParameter (
        Parameter::PROCESSOR_SCOPE,
        "laser_host",
        "Laser Host",
        "Numeric IPv4 address of the LaserDriver Pi",
        "192.168.17.10",
        true);

    addIntParameter (
        Parameter::PROCESSOR_SCOPE,
        "laser_udp_port",
        "Port",
        "UDP trigger port of laserhat-brokerd on the LaserDriver Pi (--udp)",
        LaserTriggerUdp::DEFAULT_PORT,
        1,
        65535,
        true);

    addIntParameter (
        Parameter::PROCESSOR_SCOPE,
        "laser_port",
        "Port",
        "Port of the LaserDriver Pi's web GUI (HTTP transport)",
        LaserTriggerHttp::DEFAULT_PORT,
        1,
        65535,
        true);

    /* EMG / ACC Movement Detection Settings */
    addCategoricalParameter (
        Parameter::STREAM_SCOPE,
        "mov_detect",
        "Mov. Detect",
        "If OFF is selected, the mechanism of event blockage based on movement detection is disabled and ripples are not silenced. \
		If ACC is selected, the RMS of all selected channels is used to calculate the magnitude of the acceleration vector. \
		If EMG is selected, an input channel is designated",
        { "OFF", "ACC", "EMG" },
        0);

    addSelectedChannelsParameter (
        Parameter::STREAM_SCOPE,
        "mov_input",
        "Mov. Input",
        "the channel(s) to use for movement detection (one channel for EMG, up to three for ACC)",
        3);

    // Movement data can come from another stream, e.g. the headstage IMU's. Both lists are filled in
    // updateSettings(); the placeholders only need to be long enough for a saved choice to load.
    {
        Array<String> streams { "This stream" };
        for (int i = 1; i <= 64; i++)
            streams.add (String (i));

        addCategoricalParameter (
            Parameter::STREAM_SCOPE,
            "mov_source",
            "Mov. Source",
            "Stream the movement channels come from: this stream (choose them with Mov. Input) or another stream, such as \
the headstage IMU's when it is present (choose them with Mov. Axes). Another stream's data is read at its own rate.",
            streams,
            0);

        Array<String> axes;
        for (int i = 1; i <= MAX_NOISE_CHOICES; i++)
            axes.add (String (i));

        addCategoricalParameter (
            Parameter::STREAM_SCOPE,
            "mov_axes",
            "Mov. Axes",
            "Movement channels from the Mov. Source stream: an x/y/z group (for ACC, the magnitude of the vector) or a single \
channel (for EMG).",
            axes,
            0);
    }

    addTtlLineParameter (
        Parameter::STREAM_SCOPE,
        "mov_out",
        "Mov. Output",
        "output TTL channel that indicates the period when ripple detection is silenced by movement (OFF if events are not blocked, 1 if events are blocked).",
        16);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "mov_std",
        "Mov. Std Dev",
        "Number of standard deviations above the average to be the amplitude threshold for the EMG/ACC",
        "",
        5,
        0,
        9999,
        1);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "min_time_st",
        "Min Time Steady",
        "Minimum period (in ms) of immobility (RMS below the amplitude threshold) required to enable ripple detection again after movement is detected.",
        "ms",
        5000,
        0,
        999999,
        1);

    addFloatParameter (
        Parameter::STREAM_SCOPE,
        "min_time_mov",
        "Min Move Time",
        "Minimum period (in ms) during which the RMS values of EMG/accelerometer must be above the corresponding amplitude threshold for movement detection (and ripple silencing).",
        "ms",
        10,
        0,
        999999,
        1);
}

// Update settings
void RippleDetector::updateSettings()
{
    const ScopedValueSetter<bool> updating (updatingSettings, true);
    firedStreams.reserve (256);

    settings.update (getDataStreams());
    configureLaserTrigger (true);

    for (auto stream : getDataStreams())
    {
        const uint16 streamId = stream->getStreamId();
        RippleDetectorSettings* s = settings[streamId];

        s->params = DetectionParams();
        s->params.sampleRate = stream->getSampleRate();

        s->method = createDetectionMethod (getDetectionMethodNames()[0]);
        s->method->setParams (s->params);
        s->method->reset();

        s->noiseVeto = std::make_unique<NoiseVeto> (createDetectionMethod (getDetectionMethodNames()[0]));
        s->noiseVeto->setParams (s->params);
        s->noiseVeto->reset();
        s->viewerFifo.setCapacity (stream->getSampleRate(), VIEWER_FIFO_SECONDS);

        s->rippleTtlHigh = false;
        s->pluginEnabled = true;
        s->isCalibrating = true;
        s->calibrate = true;
        s->pointsProcessed = 0;
        s->calibrationPoints = (int) (stream->getSampleRate() * CALIBRATION_DURATION_SECONDS);
        s->calibrationProgress = 0.0f;

        s->movBaseline.clear();
        s->counterMovUpThresh = 0;
        s->counterMovDownThresh = 0;
        s->flagMovMinTimeUp = false;
        s->flagMovMinTimeDown = false;
        s->movementChannels.clear();

        // Derived channels can only be created here, so read the parameter directly
        s->featureChannel = nullptr;
        s->thresholdChannel = nullptr;
        s->eventChannelOut = nullptr;
        s->featureOutputActive = (bool) stream->getParameter ("feature_out")->getValue();

        if (s->featureOutputActive)
            addFeatureChannels (getDataStream (streamId));

        // The channel drop-downs list the stream's own channel names
        setChannelChoices (stream, "ripple_channel", false);
        setChannelChoices (stream, "noise_channel", true);
        parameterValueChanged (stream->getParameter ("ripple_channel"));
        parameterValueChanged (stream->getParameter ("noise_channel"));
        parameterValueChanged (stream->getParameter ("stim_in"));

        s->pulseParams = PulseEdgeDetector::Params();
        s->pulseParams.sampleRate = stream->getSampleRate();
        parameterValueChanged (stream->getParameter ("pulse_thresh"));
        parameterValueChanged (stream->getParameter ("pulse_lockout"));
        parameterValueChanged (stream->getParameter ("periodic_count"));
        parameterValueChanged (stream->getParameter ("periodic_interval"));
        s->testParamsDirty = true; // applied with the current mode at the first block
        s->testRestart = true;
        s->externalOnsets.reserve (64);
        parameterValueChanged (stream->getParameter ("detect_mode"));
        s->testModeActive = s->testModeRequested;
        parameterValueChanged (stream->getParameter ("Ripple_Out"));
        parameterValueChanged (stream->getParameter ("ripple_std"));
        parameterValueChanged (stream->getParameter ("time_thresh"));
        parameterValueChanged (stream->getParameter ("refr_time"));
        parameterValueChanged (stream->getParameter ("rms_samples"));
        parameterValueChanged (stream->getParameter ("feature_out"));
        parameterValueChanged (stream->getParameter ("baseline"));
        parameterValueChanged (stream->getParameter ("adapt_tau"));
        parameterValueChanged (stream->getParameter ("mov_input"));
        setMovementSourceChoices (stream);
        updateMovementChannels (streamId);
        parameterValueChanged (stream->getParameter ("mov_detect"));
        parameterValueChanged (stream->getParameter ("mov_out"));
        parameterValueChanged (stream->getParameter ("mov_std"));
        parameterValueChanged (stream->getParameter ("min_time_st"));
        parameterValueChanged (stream->getParameter ("min_time_mov"));

        //Add event channels to use for detection data
        EventChannel::Settings evSettings {
            EventChannel::Type::TTL,
            "Ripple detector output",
            "Triggers when a ripple or movement is detected on the input channel",
            "dataderived.ripple",
            getDataStream (streamId)
        };
        eventChannels.add (new EventChannel (evSettings));
        eventChannels.getLast()->addProcessor (this);
        s->eventChannel = eventChannels.getLast();
    }
}

namespace
{
/** Shows a message box once the current message (e.g. a parameter change) has been handled. Showing it
    from inside a parameter handler could suspend the handler until the box was dismissed, leaving the
    setting half-applied (and an HTTP API request waiting). */
void showAlertLater (MessageBoxIconType icon, const String& title, const String& message)
{
    MessageManager::callAsync ([icon, title, message]
                               { AlertWindow::showMessageBoxAsync (icon, title, message); });
}
} // namespace

void RippleDetector::setChoicesKeepingName (CategoricalParameter* param, const Array<String>& choices, bool firstIsFixed)
{
    if (param == nullptr)
        return;

    // The entry chosen before, by name (the categories are still the previous ones)
    const Array<String> previous = param->getCategories();
    const int previousIndex = param->getSelectedIndex();
    const String previousName = isPositiveAndBelow (previousIndex, previous.size()) ? previous[previousIndex] : String();

    param->setCategories (choices);

    // Keep the choice on the same name if its position moved. A fixed first entry ("None", "This
    // stream") stays put, and a name that is no longer there (e.g. a placeholder from a loaded
    // configuration) keeps the position.
    if (previousName.isNotEmpty() && ! (firstIsFixed && previousIndex == 0))
    {
        const int moved = choices.indexOf (previousName);
        if (moved >= 0 && moved != param->getSelectedIndex())
            param->currentValue = moved; // set directly: this can run inside updateSettings()
    }
}

void RippleDetector::setChannelChoices (const DataStream* stream, const String& paramName, bool withNone)
{
    Array<String> choices;
    if (withNone)
        choices.add ("None");
    for (auto* channel : stream->getContinuousChannels())
        choices.add (channel->getName());

    setChoicesKeepingName ((CategoricalParameter*) stream->getParameter (paramName), choices, withNone);
}

void RippleDetector::setMovementSourceChoices (const DataStream* stream)
{
    RippleDetectorSettings* s = settings[stream->getStreamId()];

    // Mov. Source: this stream, then the others by name
    Array<String> sources { "This stream" };
    Array<uint16> sourceIds { 0 };
    for (auto other : getDataStreams())
    {
        if (other->getStreamId() == stream->getStreamId())
            continue;
        sources.add (other->getName());
        sourceIds.add (other->getStreamId());
    }

    auto* sourceParam = (CategoricalParameter*) stream->getParameter ("mov_source");
    setChoicesKeepingName (sourceParam, sources, true);
    const int chosen = sourceParam->getSelectedIndex();
    s->movSourceStreamId = isPositiveAndBelow (chosen, sourceIds.size()) ? sourceIds[chosen] : 0;

    // Mov. Axes: the chosen stream's x/y/z groups (e.g. IMU_A_ACC_X/Y/Z), then its single channels
    Array<String> axes;
    s->movAxesChannels.clear();

    if (const DataStream* source = s->movSourceStreamId != 0 ? getDataStream (s->movSourceStreamId) : nullptr)
    {
        const auto channels = source->getContinuousChannels();

        for (auto* x : channels)
        {
            const String name = x->getName();
            if (! name.endsWith ("_X"))
                continue;

            const String prefix = name.dropLastCharacters (2);
            const ContinuousChannel* y = nullptr;
            const ContinuousChannel* z = nullptr;
            for (auto* c : channels)
            {
                if (c->getName() == prefix + "_Y")
                    y = c;
                else if (c->getName() == prefix + "_Z")
                    z = c;
            }

            if (y != nullptr && z != nullptr)
            {
                axes.add (prefix + "_X/Y/Z");
                s->movAxesChannels.push_back ({ x->getGlobalIndex(), y->getGlobalIndex(), z->getGlobalIndex() });
            }
        }

        for (auto* c : channels)
        {
            axes.add (c->getName());
            s->movAxesChannels.push_back ({ c->getGlobalIndex() });
        }
    }

    // Without another stream, leave the list as it is (Mov. Axes is hidden then). Shrinking it here
    // would lose a saved choice: while a configuration loads, the GUI runs this update before it
    // applies the saved values, and it rejects a saved index beyond the current list.
    if (axes.isEmpty())
        return;

    setChoicesKeepingName ((CategoricalParameter*) stream->getParameter ("mov_axes"), axes, false);
}

void RippleDetector::updateMovementChannels (uint16 streamId)
{
    RippleDetectorSettings* s = settings[streamId];

    if (s->movSourceStreamId == 0)
    {
        s->movementChannels = s->movInputChannels;
    }
    else
    {
        const int choice = ((CategoricalParameter*) getDataStream (streamId)->getParameter ("mov_axes"))->getSelectedIndex();
        s->movementChannels = isPositiveAndBelow (choice, (int) s->movAxesChannels.size()) ? s->movAxesChannels[(size_t) choice]
                                                                                            : std::vector<int>();
    }

    s->movChannChanged = true;
}

void RippleDetector::addFeatureChannels (DataStream* stream)
{
    RippleDetectorSettings* s = settings[stream->getStreamId()];

    // Use the resolution of the stream's own channels so recordings keep the same scale
    const float bitVolts = stream->getChannelCount() > 0 ? stream->getContinuousChannels()[0]->getBitVolts() : 0.05f;

    auto addChannel = [&] (const String& name, const String& description, const String& identifier) -> ContinuousChannel*
    {
        ContinuousChannel::Settings chSettings {
            ContinuousChannel::Type::ELECTRODE,
            name,
            description,
            identifier,
            bitVolts,
            stream
        };

        continuousChannels.add (new ContinuousChannel (chSettings));
        continuousChannels.getLast()->addProcessor (this);
        return continuousChannels.getLast();
    };

    s->featureChannel = addChannel ("RIP_FEAT",
                                    "Windowed RMS of the ripple input channel",
                                    "dataderived.ripple.feature");
    s->thresholdChannel = addChannel ("RIP_THR",
                                      "Ripple detection threshold: baseline mean + Onset Std Dev x SD (0 during calibration)",
                                      "dataderived.ripple.threshold");
    s->eventChannelOut = addChannel ("RIP_EVENT",
                                     "Equal to the threshold while a ripple is detected (TTL high), 0 otherwise",
                                     "dataderived.ripple.event");
}

// Create and return editor
AudioProcessorEditor* RippleDetector::createEditor()
{
    editor = std::make_unique<RippleDetectorEditor> (this);
    return editor.get();
}

float RippleDetector::getCalibrationProgress (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return -1.0f;

    return settings[streamId]->calibrationProgress.load();
}

bool RippleDetector::isCalibrating (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return false;

    return settings[streamId]->isCalibrating;
}

double RippleDetector::getBaselineMean (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr || settings[streamId]->method == nullptr || settings[streamId]->isCalibrating)
        return 0.0;

    return settings[streamId]->method->getBaselineMean();
}

double RippleDetector::getBaselineStd (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr || settings[streamId]->method == nullptr || settings[streamId]->isCalibrating)
        return 0.0;

    return settings[streamId]->method->getBaselineStd();
}

bool RippleDetector::hasNoiseChannel (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return false;

    return settings[streamId]->noiseInputChannel >= 0;
}

double RippleDetector::getNoiseBaselineMean (uint16 streamId)
{
    if (! hasNoiseChannel (streamId) || settings[streamId]->isCalibrating)
        return 0.0;

    return settings[streamId]->noiseVeto->getMethod().getBaselineMean();
}

double RippleDetector::getNoiseBaselineStd (uint16 streamId)
{
    if (! hasNoiseChannel (streamId) || settings[streamId]->isCalibrating)
        return 0.0;

    return settings[streamId]->noiseVeto->getMethod().getBaselineStd();
}

uint32_t RippleDetector::getVetoedOnsets (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return 0;

    return settings[streamId]->vetoedOnsets.load();
}

int RippleDetector::getTestMode (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return 0;

    return settings[streamId]->testModeRequested;
}

RippleDetector::TestStats RippleDetector::getTestStats (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return {};

    const RippleDetectorSettings* s = settings[streamId];
    return { s->testTriggered.load(), s->periodicSent.load(), s->pulseParams.thresholdUv, s->pulseParams.lockoutMs,
             s->periodicCount, s->periodicIntervalS };
}

FeatureFifo* RippleDetector::getFeatureFifo (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return nullptr;

    return &settings[streamId]->viewerFifo;
}

const DetectionParams* RippleDetector::getStreamParams (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return nullptr;

    return &settings[streamId]->params;
}

void RippleDetector::refreshEditor()
{
    // During updateSettings() the GUI refreshes the editor afterwards; doing it now would
    // reach streams that are being replaced (and crashed the GUI when a File Reader switched files)
    if (updatingSettings)
        return;

    // Show only the parameters relevant for the selected method / baseline mode
    if (auto* ed = dynamic_cast<RippleDetectorEditor*> (getEditor()))
    {
        if (MessageManager::getInstance()->isThisTheMessageThread())
            ed->updateMethodView();
        else
            MessageManager::callAsync ([ed]
                                       { ed->updateMethodView(); });
    }
}

void RippleDetector::applyParams (uint16 streamId)
{
    // Parameters are handed to the methods from the audio thread (see process()),
    // because setParams() may resize internal buffers.
    settings[streamId]->paramsDirty = true;
}

void RippleDetector::configureLaserTrigger (bool destinationChanged)
{
    // The destination is only rewritten from the host, transport and port
    // parameters, which are locked during acquisition, so fire() on the audio
    // thread never sees it change. The on/off switch can flip at any time and
    // only touches an atomic.
    if (destinationChanged)
    {
        const bool http = getParameter ("laser_transport")->getValueAsString().equalsIgnoreCase ("HTTP");
        laserTrigger.configure (getParameter ("laser_host")->getValueAsString(),
                                (int) getParameter (http ? "laser_port" : "laser_udp_port")->getValue(),
                                http ? LaserTrigger::Transport::Http : LaserTrigger::Transport::Udp);
    }

    const bool enabled = (bool) getParameter ("laser_trigger")->getValue();
    laserTrigger.setEnabled (enabled);

    if (enabled && ! laserTrigger.isActive())
    {
        LOGE ("Laser Trigger: \"", getParameter ("laser_host")->getValueAsString(),
              "\" is not a numeric IPv4 address; no triggers will be sent");
        CoreServices::sendStatusMessage ("Laser Trigger: set Laser Host to the Pi's IPv4 address");
    }
}

int RippleDetector::getStimInputLine (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return -1;

    return settings[streamId]->stimInputLine;
}

StimLatencyMeter::Stats RippleDetector::getLatencyStats (uint16 streamId)
{
    if (streamId == 0 || getDataStream (streamId) == nullptr)
        return {};

    return settings[streamId]->latency.getStats();
}

void RippleDetector::handleTTLEvent (TTLEventPtr event)
{
    RippleDetectorSettings* s = settings[event->getStreamId()];

    if (s == nullptr || ! event->getState())
        return;

    if (s->stimInputLine >= 0 && event->getLine() == s->stimInputLine)
        s->latency.hardwareEdge (event->getSampleNumber());
}

bool RippleDetector::startAcquisition()
{
    for (auto stream : getDataStreams())
    {
        RippleDetectorSettings* s = settings[stream->getStreamId()];
        s->vetoedOnsets = 0;
        // Each run starts the test sequence afresh (the periodic schedule, the counts)
        s->testRestart = true;
        s->testTriggered = 0;
        s->testIgnored = 0;
        s->periodicSent = 0;
        s->latency.reset (stream->getSampleRate(), (int64) (stream->getSampleRate() * LATENCY_TIMEOUT_SECONDS));
    }

    return true;
}

bool RippleDetector::stopAcquisition()
{
    for (auto stream : getDataStreams())
    {
        RippleDetectorSettings* s = settings[stream->getStreamId()];
        // (LOGC expands with its own semicolon, so if / else need braces)
        if (s->testModeActive == TEST_PULSE)
        {
            LOGC ("Pulse test mode (", stream->getName(), "): ", (int) s->testTriggered.load(), " pulses detected");
        }
        else if (s->testModeActive == TEST_PERIODIC)
        {
            LOGC ("Periodic test mode (", stream->getName(), "): ", (int) s->periodicSent.load(), " of ", s->periodicCount,
                  " responses sent, every ", s->periodicIntervalS, " s");
        }
        else if (s->noiseInputChannel >= 0)
        {
            LOGC ("Noise veto (", stream->getName(), "): ", (int) s->vetoedOnsets.load(), " ripple onsets suppressed");
        }

        const auto lat = s->latency.getStats();
        if (s->stimInputLine >= 0 && (lat.matched > 0 || lat.missed > 0))
            LOGC ("Stimulation latency (", stream->getName(), ", line ", s->stimInputLine + 1, "): ", (int) lat.matched,
                  " paired, mean ", lat.meanMs, " ms (min ", lat.minMs, ", max ", lat.maxMs, ") from the ripple TTL, ",
                  lat.meanDecisionMs, " ms from the detection decision; ", (int) lat.missed, " onsets without a stimulus, ",
                  (int) lat.unmatched, " triggers without an onset");
    }

    const auto st = laserTrigger.getStats();

    if (st.requested > 0)
    {
        const bool udp = laserTrigger.getTransport() == LaserTrigger::Transport::Udp;
        LOGC ("Laser Trigger (", udp ? "UDP" : "HTTP", "): ", (int) st.requested, " requested, ", (int) st.fired, " fired (",
              (int) st.busy, " while the MCU was mid-pulse, so ignored), ", (int) st.rejected, " rejected by the Pi, ",
              (int) st.failed, " unanswered; ", udp ? "round trip" : "request to Pi reply", " mean ", st.meanMs,
              " ms, max ", st.maxMs, " ms");
        if (udp && st.fired > 0)
            LOGC ("Laser Trigger: Pi receive to GPIO edge mean ", st.meanPiUs, " us, max ", st.maxPiUs, " us");
    }

    return true;
}

void RippleDetector::parameterValueChanged (Parameter* param)
{
    String paramName = param->getName();

    if (param->getScope() == Parameter::PROCESSOR_SCOPE)
    {
        if (paramName.equalsIgnoreCase ("laser_trigger"))
            configureLaserTrigger (false);
        else if (paramName.startsWithIgnoreCase ("laser_"))
            configureLaserTrigger (true);

        if (paramName.equalsIgnoreCase ("laser_transport"))
            refreshEditor(); // shows the port for the chosen transport
        return;
    }

    uint16 streamId = param->getStreamId();
    RippleDetectorSettings* s = settings[streamId];

    if (paramName.equalsIgnoreCase ("ripple_channel"))
    {
        // Entry i is the stream's channel i; a stream without channels has none to choose
        const int choice = ((CategoricalParameter*) param)->getSelectedIndex();
        const auto channels = getDataStream (streamId)->getContinuousChannels();

        if (choice >= 0 && choice < channels.size())
            s->rippleInputChannel = channels[choice]->getGlobalIndex();
        else
            s->rippleInputChannel = -1;
    }
    else if (paramName.equalsIgnoreCase ("detect_mode"))
    {
        // Picked up by the audio thread at its next block (process())
        s->testModeRequested = jlimit (0, 2, ((CategoricalParameter*) param)->getSelectedIndex());
        refreshEditor();
    }
    else if (paramName.equalsIgnoreCase ("pulse_thresh"))
    {
        s->pulseParams.thresholdUv = (float) param->getValue();
        s->testParamsDirty = true;
    }
    else if (paramName.equalsIgnoreCase ("pulse_lockout"))
    {
        s->pulseParams.lockoutMs = (float) param->getValue();
        s->testParamsDirty = true;
    }
    else if (paramName.equalsIgnoreCase ("periodic_count"))
    {
        s->periodicCount = (int) param->getValue();
        s->testParamsDirty = true;
    }
    else if (paramName.equalsIgnoreCase ("periodic_interval"))
    {
        s->periodicIntervalS = (float) param->getValue();
        s->testParamsDirty = true;
    }
    else if (paramName.equalsIgnoreCase ("stim_in"))
    {
        s->stimInputLine = (int) param->getValue();
    }
    else if (paramName.equalsIgnoreCase ("noise_channel"))
    {
        // Entry 0 is "None"; entry i is the stream's channel i - 1
        const int choice = ((CategoricalParameter*) param)->getSelectedIndex();
        const auto channels = getDataStream (streamId)->getContinuousChannels();
        int channel = -1;

        if (choice > 0 && choice <= channels.size())
            channel = channels[choice - 1]->getGlobalIndex();

        if (channel >= 0 && channel == s->rippleInputChannel)
        {
            // A channel always crosses threshold with itself, which would veto every ripple
            LOGE ("Noise Input is the same channel as Ripple Input; ignoring it");
            CoreServices::sendStatusMessage ("Ripple Detector: the noise channel must differ from the ripple channel");
            channel = -1;
        }

        if (channel != s->noiseInputChannel)
        {
            // The noise channel needs its own baseline; recalibrate the stream
            s->noiseInputChannel = channel;
            s->calibrate = true;
        }
    }
    else if (paramName.equalsIgnoreCase ("Ripple_Out"))
    {
        s->rippleOutputChannel = (int) param->getValue();
    }
    else if (paramName.equalsIgnoreCase ("ripple_std"))
    {
        s->params.onsetSds = (float) param->getValue();
        applyParams (streamId);
    }
    else if (paramName.equalsIgnoreCase ("time_thresh"))
    {
        s->params.minDurationMs = (int) param->getValue();
        applyParams (streamId);
    }
    else if (paramName.equalsIgnoreCase ("refr_time"))
    {
        s->params.refractoryMs = (int) param->getValue();
        applyParams (streamId);
    }
    else if (paramName.equalsIgnoreCase ("rms_samples"))
    {
        s->params.rmsSamples = (int) param->getValue();
        applyParams (streamId);
    }
    else if (paramName.equalsIgnoreCase ("feature_out"))
    {
        s->featureOutputRequested = (bool) param->getValue();

        // Channels are created in updateSettings(), so ask for a rebuild if the state differs.
        // This is asynchronous because parameterValueChanged() is also called from updateSettings().
        if (s->featureOutputRequested != s->featureOutputActive)
        {
            MessageManager::callAsync ([this]
                                       { CoreServices::updateSignalChain (this); });
        }

        refreshEditor();
    }
    else if (paramName.equalsIgnoreCase ("baseline"))
    {
        s->params.adaptiveBaseline = ((CategoricalParameter*) param)->getValueAsString().equalsIgnoreCase ("Adaptive");
        applyParams (streamId);
        refreshEditor();
    }
    else if (paramName.equalsIgnoreCase ("adapt_tau"))
    {
        s->params.adaptTauSeconds = (float) param->getValue();
        applyParams (streamId);
    }
    else if (paramName.equalsIgnoreCase ("mov_detect"))
    {
        s->movSwitch = ((CategoricalParameter*) param)->getValueAsString();

        int movementChannelCount = (int) s->movementChannels.size();

        if (s->movSwitch.equalsIgnoreCase ("ACC"))
        {
            String msg;
            if (! movementChannelCount)
            {
                s->movSwitch = "OFF";
                ((CategoricalParameter*) param)->setNextValue ("OFF");
                msg += "Movement detection via acceleration magnitude requires at least one input channel. ";
                msg += "Choose up to 3 channels with Mov. Input, or an x/y/z group with Mov. Axes for another stream. ";
                msg += "Switching to OFF until then.";
                showAlertLater (AlertWindow::WarningIcon, "WARNING", msg);
            }
            else
            {
                msg += String (movementChannelCount);
                msg += movementChannelCount > 1 ? " channels " : " channel ";
                msg += "currently selected for movement detection via acceleration magnitude (ACC)";
                msg += "\n\n";
                msg += "You may use up to 3 channels to compute the acceleration magnitude \n\n";
                msg += "Use Mov. Input (or Mov. Axes for another stream) to change them.";
                showAlertLater (AlertWindow::InfoIcon, "INFO", msg);
            }
        }
        else if (s->movSwitch.equalsIgnoreCase ("EMG"))
        {
            String msg;
            if (movementChannelCount != 1)
            {
                s->movSwitch = "OFF";
                ((CategoricalParameter*) param)->setNextValue ("OFF");
                msg += "Movement detection via EMG requires exactly one input channel. ";
                msg += "Choose one channel with Mov. Input, or a single channel with Mov. Axes for another stream. ";
                msg += "Switching to OFF until then.";
                showAlertLater (AlertWindow::WarningIcon, "WARNING", msg);
            }
            else
            {
                msg += "EMG movement detection is enabled.";
                showAlertLater (AlertWindow::InfoIcon, "INFO", msg);
            }
        }
        s->movSwitchEnabled = ! (s->movSwitch).equalsIgnoreCase ("OFF");
        s->movChannChanged = true;
        refreshEditor();
    }
    else if (paramName.equalsIgnoreCase ("mov_input"))
    {
        s->movInputChannels.clear();
        Array<var>* array = param->getValue().getArray();

        const auto channels = getDataStream (streamId)->getContinuousChannels();
        for (int i = 0; i < array->size(); i++)
        {
            // Skip a saved selection past the end of a stream with fewer channels
            const int localIndex = int (array->getReference (i));
            if (localIndex >= 0 && localIndex < channels.size())
                s->movInputChannels.push_back (channels[localIndex]->getGlobalIndex());
        }

        if (! updatingSettings)
            updateMovementChannels (streamId);
    }
    else if (paramName.equalsIgnoreCase ("mov_source"))
    {
        // The axes list follows the chosen stream (in updateSettings() this is done afterwards)
        if (! updatingSettings)
        {
            setMovementSourceChoices (getDataStream (streamId));
            updateMovementChannels (streamId);
            refreshEditor();
        }
    }
    else if (paramName.equalsIgnoreCase ("mov_axes"))
    {
        if (! updatingSettings)
            updateMovementChannels (streamId);
    }
    else if (paramName.equalsIgnoreCase ("mov_out"))
    {
        s->movementOutputChannel = (int) param->getValue();
    }
    else if (paramName.equalsIgnoreCase ("mov_std"))
    {
        s->movSds = (float) param->getValue();
    }
    else if (paramName.equalsIgnoreCase ("min_time_st"))
    {
        s->minTimeWoMov = (int) param->getValue();
        s->minMovSamplesBelowThresh =
            (int) std::ceil (getDataStream (streamId)->getSampleRate() * s->minTimeWoMov / 1000);
    }
    else if (paramName.equalsIgnoreCase ("min_time_mov"))
    {
        s->minTimeWMov = (int) param->getValue();
        s->minMovSamplesAboveThresh =
            (int) std::ceil (getDataStream (streamId)->getSampleRate() * s->minTimeWMov / 1000);
    }
}

// Data acquisition and manipulation loop
void RippleDetector::process (AudioBuffer<float>& buffer)
{
    // Upstream TTLs, for the stimulation latency (handleTTLEvent)
    checkForEvents();
    firedStreams.clear();

    // The calibrate button applies to every stream
    const bool calibrateAll = shouldCalibrate.exchange (false);

    for (auto stream : getDataStreams())
    {
        if (! (*stream)["enable_stream"])
            continue;

        const uint16 streamId = stream->getStreamId();
        const int64 firstSampleInBlock = getFirstSampleNumberForBlock (streamId);
        const int numSamplesInBlock = (int) getNumSamplesInBlock (streamId);
        RippleDetectorSettings* s = settings[streamId];

        // Derived channels must always hold defined values, even when detection is idle
        float* featureOut = nullptr;
        float* thresholdOut = nullptr;
        float* eventOut = nullptr;

        if (s->featureOutputActive && s->featureChannel != nullptr && numSamplesInBlock > 0)
        {
            featureOut = buffer.getWritePointer (s->featureChannel->getGlobalIndex());
            thresholdOut = buffer.getWritePointer (s->thresholdChannel->getGlobalIndex());
            eventOut = buffer.getWritePointer (s->eventChannelOut->getGlobalIndex());

            FloatVectorOperations::clear (featureOut, numSamplesInBlock);
            FloatVectorOperations::clear (thresholdOut, numSamplesInBlock);
            FloatVectorOperations::clear (eventOut, numSamplesInBlock);
        }

        if (s->rippleInputChannel < 0 || s->method == nullptr || numSamplesInBlock == 0)
            continue;

        if (s->paramsDirty.exchange (false))
        {
            s->method->setParams (s->params);
            s->noiseVeto->setParams (s->params);
        }

        // Scratch buffers for this block
        if ((int) s->featureScratch.size() < numSamplesInBlock)
        {
            s->featureScratch.resize ((size_t) numSamplesInBlock);
            s->zScratch.resize ((size_t) numSamplesInBlock);
            s->flagScratch.resize ((size_t) numSamplesInBlock);
            s->noiseFeatureScratch.resize ((size_t) numSamplesInBlock);
        }

        // Switching between ripple detection and the test modes
        const int wantMode = s->testModeRequested.load();
        if (wantMode != s->testModeActive)
            s->testParamsDirty = true; // the lockout depends on the mode

        if (s->testParamsDirty.exchange (false))
        {
            // Periodic onsets must never fall in the lockout: keep it shorter than the interval
            PulseEdgeDetector::Params p = s->pulseParams;
            if (wantMode == TEST_PERIODIC)
                p.lockoutMs = 0.9 * s->periodicIntervalS * 1000.0;
            s->pulse.setParams (p);
            s->periodic.configure (p.sampleRate, s->periodicCount, s->periodicIntervalS);
        }

        if (wantMode != s->testModeActive)
        {
            const bool wasTest = s->testModeActive != TEST_OFF;
            s->testModeActive = wantMode;

            if (s->rippleTtlHigh)
                setRippleTtl (streamId, false, 0, firstSampleInBlock);

            if (wantMode != TEST_OFF)
            {
                s->testRestart = true;

                // Movement gating is bypassed in the test modes: release a block that is in force
                if (! s->pluginEnabled)
                {
                    s->pluginEnabled = true;
                    addEvent (s->createEvent (s->movementOutputChannel, firstSampleInBlock, false), 0);
                }
                LOGC ("Stream ", streamId, wantMode == TEST_PULSE ? ": pulse test mode" : ": periodic test mode");
            }
            else if (wasTest)
            {
                // The ripple baseline predates the test; estimate it again
                s->calibrate = true;
                LOGC ("Stream ", streamId, ": ripple detection");
            }
        }

        if (s->testModeActive != TEST_OFF)
        {
            const float* data = buffer.getReadPointer (s->rippleInputChannel, 0);
            processTestTrigger (streamId, data, numSamplesInBlock, firstSampleInBlock, featureOut, thresholdOut, eventOut);
            s->latency.expire (firstSampleInBlock + numSamplesInBlock);
            continue;
        }

        // Enable detection again if movement detection was switched off or if calibration was requested
        if (! s->pluginEnabled && (! s->movSwitchEnabled || calibrateAll))
        {
            s->pluginEnabled = true;
            addEvent (s->createEvent (s->movementOutputChannel, firstSampleInBlock, false), 0);
        }

        // Check if need to calibrate
        const bool movementChanged = s->movChannChanged && s->movSwitchEnabled;
        s->movChannChanged = false;

        if (calibrateAll || s->calibrate || movementChanged)
        {
            LOGC ("Calibrating stream ", streamId, "...");

            s->isCalibrating = true;
            s->calibrate = false;
            s->pointsProcessed = 0;
            s->calibrationProgress = 0.0f;

            s->method->reset();
            s->noiseVeto->reset();
            s->movBaseline.clear();

            if (s->rippleTtlHigh)
                setRippleTtl (streamId, false, 0, firstSampleInBlock);
        }

        // Movement gating is evaluated first so that it applies to this block's ripple events
        if (s->movSwitchEnabled)
            processMovement (streamId, buffer, numSamplesInBlock, firstSampleInBlock);

        const float* rippleData = buffer.getReadPointer (s->rippleInputChannel, 0);
        const float* noiseData = s->noiseInputChannel >= 0 ? buffer.getReadPointer (s->noiseInputChannel, 0) : nullptr;
        processRipples (streamId, rippleData, noiseData, numSamplesInBlock, firstSampleInBlock, featureOut, eventOut);
        s->latency.expire (firstSampleInBlock + numSamplesInBlock);

        // The threshold is only defined once calibration has finished
        if (thresholdOut != nullptr && ! s->isCalibrating)
            FloatVectorOperations::fill (thresholdOut, (float) s->method->getOnsetThreshold(), numSamplesInBlock);

        publishFeatures (streamId, rippleData, noiseData, numSamplesInBlock);

        if (s->isCalibrating)
        {
            s->pointsProcessed += numSamplesInBlock;
            s->calibrationProgress = std::min (1.0f, (float) s->pointsProcessed / (float) std::max (1, s->calibrationPoints));

            if (s->pointsProcessed >= s->calibrationPoints)
            {
                s->isCalibrating = false;
                s->calibrationProgress = 1.0f;
                s->method->finishCalibration();
                s->noiseVeto->finishCalibration();
                s->movBaseline.finish();
                logCalibration (streamId);
            }
        }
    }

    relayOnsets();
}

void RippleDetector::processRipples (uint16 streamId, const float* rippleData, const float* noiseData, int numSamples, int64 firstSample, float* featureOut, float* eventOut)
{
    RippleDetectorSettings* s = settings[streamId];

    uint8_t baseFlags = 0;
    if (s->isCalibrating)
        baseFlags |= FeatureFifo::CALIBRATING;
    if (! s->pluginEnabled)
        baseFlags |= FeatureFifo::BLOCKED;
    std::fill (s->flagScratch.begin(), s->flagScratch.begin() + numSamples, baseFlags);

    // Window length the methods use for this block, for marking noise / veto windows in the viewer
    const int window = std::max (1, std::min (s->params.rmsSamples, numSamples));
    auto markWindow = [&] (int from, uint8_t flag)
    {
        for (int k = from; k < std::min (from + window, numSamples); k++)
            s->flagScratch[(size_t) k] |= flag;
    };

    if (s->isCalibrating)
    {
        s->method->calibrate (rippleData, numSamples, s->featureScratch.data());
        if (noiseData != nullptr)
            s->noiseVeto->calibrate (noiseData, numSamples, s->noiseFeatureScratch.data());
    }
    else
    {
        s->events.clear();
        s->method->process (rippleData, numSamples, s->events, s->featureScratch.data());

        if (noiseData != nullptr)
        {
            s->noiseVeto->process (noiseData, numSamples, s->noiseFeatureScratch.data());
            for (int w : s->noiseVeto->getNoiseWindows())
                markWindow (w, FeatureFifo::NOISE);
        }

        // Apply the events to the TTL line and record the line state per sample
        const float eventLevel = (float) s->method->getOnsetThreshold();
        int cursor = 0;
        bool high = s->rippleTtlHigh;

        auto markHigh = [&] (int from, int to)
        {
            for (int k = from; k < to; k++)
                s->flagScratch[(size_t) k] |= FeatureFifo::TTL_HIGH;
            if (eventOut != nullptr && to > from)
                FloatVectorOperations::fill (eventOut + from, eventLevel, to - from);
        };

        for (const auto& ev : s->events)
        {
            bool newState = high;

            if (ev.state)
            {
                // Onsets are blocked while movement is detected, and vetoed when
                // the noise channel crossed threshold in the same window
                if (s->pluginEnabled && ! high)
                {
                    if (noiseData != nullptr && s->noiseVeto->vetoes (ev.sampleIndex))
                    {
                        s->vetoedOnsets++;
                        markWindow (ev.sampleIndex, FeatureFifo::VETOED);
                    }
                    else
                    {
                        setRippleTtl (streamId, true, ev.sampleIndex, firstSample);
                        newState = true;

                        // The decision could only be made once the onset's window was complete
                        const int decided = std::min (ev.sampleIndex + window, numSamples);
                        s->latency.onsetEmitted (firstSample + ev.sampleIndex, firstSample + decided);
                    }
                }
            }
            else if (high)
            {
                setRippleTtl (streamId, false, ev.sampleIndex, firstSample);
                newState = false;
            }

            if (newState != high)
            {
                if (high)
                    markHigh (cursor, ev.sampleIndex);
                cursor = ev.sampleIndex;
                high = newState;
            }
        }

        if (high)
            markHigh (cursor, numSamples);
    }

    if (featureOut != nullptr)
        std::copy (s->featureScratch.begin(), s->featureScratch.begin() + numSamples, featureOut);
}

void RippleDetector::processTestTrigger (uint16 streamId, const float* data, int numSamples, int64 firstSample,
                                        float* featureOut, float* thresholdOut, float* eventOut)
{
    RippleDetectorSettings* s = settings[streamId];
    const bool periodicMode = s->testModeActive == TEST_PERIODIC;

    if (s->testRestart)
    {
        s->testRestart = false;
        s->pulse.reset();
        s->periodic.reset();
    }

    // Pulse (test): the detector finds the pulses on the channel. Periodic: the schedule gives the
    // onsets and the channel's crossings are not used.
    s->pulseEvents.clear();
    if (periodicMode)
    {
        s->externalOnsets.clear();
        s->periodic.onsets (numSamples, s->externalOnsets);
        s->pulse.process (data, numSamples, s->pulseEvents, s->featureScratch.data(), nullptr, &s->externalOnsets);
    }
    else
    {
        s->pulse.process (data, numSamples, s->pulseEvents, s->featureScratch.data());
    }

    const float threshold = (float) s->pulse.getParams().thresholdUv;
    const float eventLevel = periodicMode ? TEST_EVENT_LEVEL : threshold;

    // Drive the outputs, and mark the output TTL for the viewer
    std::fill (s->flagScratch.begin(), s->flagScratch.begin() + numSamples, (uint8_t) 0);
    bool high = s->rippleTtlHigh;
    int cursor = 0;

    auto markUntil = [&] (int to)
    {
        if (high && to > cursor)
        {
            for (int k = cursor; k < to; k++)
                s->flagScratch[(size_t) k] |= FeatureFifo::TTL_HIGH;
            if (eventOut != nullptr)
                FloatVectorOperations::fill (eventOut + cursor, eventLevel, to - cursor);
        }
        cursor = std::max (cursor, to);
    };

    for (const auto& ev : s->pulseEvents)
    {
        markUntil (ev.sampleIndex);

        if (ev.type == PulseEdgeDetector::Event::Onset)
        {
            setRippleTtl (streamId, true, ev.sampleIndex, firstSample);
            s->flagScratch[(size_t) ev.sampleIndex] |= FeatureFifo::TTL_ONSET;

            // Event time: the pulse's first sample above threshold (the scheduled sample in Periodic);
            // decision: the sample that confirmed it
            s->latency.onsetEmitted (firstSample + ev.sampleIndex - ev.edgeOffset, firstSample + ev.sampleIndex);
            high = true;
        }
        else if (high)
        {
            setRippleTtl (streamId, false, ev.sampleIndex, firstSample);
            high = false;
        }
    }
    markUntil (numSamples);

    s->testTriggered = s->pulse.getDetections();
    s->testIgnored = s->pulse.getIgnoredExternal();
    s->periodicSent = s->periodic.getSent();

    // Feature channels: the level and threshold in Pulse; only the output TTL in Periodic
    if (featureOut != nullptr)
    {
        if (periodicMode)
            FloatVectorOperations::clear (featureOut, numSamples);
        else
            std::copy (s->featureScratch.begin(), s->featureScratch.begin() + numSamples, featureOut);
    }
    if (thresholdOut != nullptr)
        FloatVectorOperations::fill (thresholdOut, periodicMode ? 0.0f : threshold, numSamples);

    // Viewer: in Pulse, the level in multiples of the threshold (the threshold line sits at 1)
    const float scale = ! periodicMode && threshold > 0.0f ? 1.0f / threshold : 0.0f;
    for (int i = 0; i < numSamples; i++)
        s->zScratch[(size_t) i] = s->featureScratch[(size_t) i] * scale;
    std::fill (s->noiseFeatureScratch.begin(), s->noiseFeatureScratch.begin() + numSamples, 0.0f);

    std::array<const float*, FeatureFifo::NUM_FEATURES> src;
    src[FeatureFifo::SIGNAL_Z] = s->zScratch.data();
    src[FeatureFifo::NOISE_Z] = s->noiseFeatureScratch.data();
    src[FeatureFifo::RAW] = data;
    src[FeatureFifo::NOISE_RAW] = s->noiseFeatureScratch.data();
    s->viewerFifo.write (src, s->flagScratch.data(), numSamples);
}

void RippleDetector::relayOnsets()
{
    // An onset on one stream (e.g. a File Reader's) can trigger a stimulus whose TTL returns on
    // another stream (the acquisition board's digital input). Streams run on separate clocks, so
    // the onset is registered on each other stream with a Stim. Input line at the newest sample
    // that stream had when the trigger was sent: its latency then runs from the moment of sending.
    for (const uint16 source : firedStreams)
    {
        for (auto stream : getDataStreams())
        {
            const uint16 id = stream->getStreamId();
            RippleDetectorSettings* s = settings[id];
            if (id == source || s->stimInputLine < 0)
                continue;

            const int64 now = getFirstSampleNumberForBlock (id) + (int64) getNumSamplesInBlock (id);
            s->latency.onsetEmitted (now, now);
        }
    }

    // Streams that are not processed (e.g. disabled here) still age their pending onsets
    for (auto stream : getDataStreams())
    {
        const uint16 id = stream->getStreamId();
        settings[id]->latency.expire (getFirstSampleNumberForBlock (id) + (int64) getNumSamplesInBlock (id));
    }
}

void RippleDetector::publishFeatures (uint16 streamId, const float* rippleData, const float* noiseData, int numSamples)
{
    RippleDetectorSettings* s = settings[streamId];

    // Each feature in its own baseline's SDs, so both share the threshold line
    auto zScore = [&] (const DetectionMethod& m, float* values)
    {
        const double mean = m.getBaselineMean();
        const double std = s->isCalibrating ? m.getRunningBaselineStd() : m.getBaselineStd();
        const float scale = std > 0.0 ? (float) (1.0 / std) : 0.0f;
        const float offset = (float) mean;
        for (int i = 0; i < numSamples; i++)
            values[i] = (values[i] - offset) * scale;
    };

    std::copy (s->featureScratch.begin(), s->featureScratch.begin() + numSamples, s->zScratch.begin());
    zScore (*s->method, s->zScratch.data());

    // The noise feature is only read here once the veto has run, so it is z-scored in place
    if (s->noiseInputChannel >= 0)
        zScore (s->noiseVeto->getMethod(), s->noiseFeatureScratch.data());
    else
        std::fill (s->noiseFeatureScratch.begin(), s->noiseFeatureScratch.begin() + numSamples, 0.0f);

    std::array<const float*, FeatureFifo::NUM_FEATURES> src;
    src[FeatureFifo::SIGNAL_Z] = s->zScratch.data();
    src[FeatureFifo::NOISE_Z] = s->noiseFeatureScratch.data();
    src[FeatureFifo::RAW] = rippleData;
    // Without a noise channel the scratch buffer is all zeros, which stands in for its raw trace
    src[FeatureFifo::NOISE_RAW] = noiseData != nullptr ? noiseData : s->noiseFeatureScratch.data();
    s->viewerFifo.write (src, s->flagScratch.data(), numSamples);
}

void RippleDetector::setRippleTtl (uint16 streamId, bool state, int sampleIndex, int64 firstSample)
{
    RippleDetectorSettings* s = settings[streamId];

    // Requested first: it is the latency path to the stimulus
    if (state)
    {
        laserTrigger.fire();
        if (firedStreams.size() < firedStreams.capacity())
            firedStreams.push_back (streamId);
    }

    addEvent (s->createEvent (s->rippleOutputChannel, firstSample + sampleIndex, state), sampleIndex);
    s->rippleTtlHigh = state;
}

void RippleDetector::processMovement (uint16 streamId, AudioBuffer<float>& buffer, int numSamples, int64 firstSample)
{
    RippleDetectorSettings* s = settings[streamId];

    if (s->movementChannels.empty())
        return;

    // The movement channels may belong to another stream (e.g. the headstage IMU's), which has its own
    // sample count in this block and its own rate. Window lengths are converted to this stream's samples,
    // so the movement times and the TTL placement stay in this stream's units (to within a block).
    const DataStream* source = s->movSourceStreamId != 0 ? getDataStream (s->movSourceStreamId) : nullptr;
    const int sourceSamples = source != nullptr ? (int) getNumSamplesInBlock (s->movSourceStreamId) : numSamples;
    if (sourceSamples <= 0)
        return;

    const double toThisStream = source != nullptr && source->getSampleRate() > 0.0f
                                    ? (double) getDataStream (streamId)->getSampleRate() / (double) source->getSampleRate()
                                    : 1.0;

    const float* movData = nullptr;

    if (s->movSwitch.equalsIgnoreCase ("ACC"))
    {
        // Magnitude of the acceleration vector over all selected channels
        s->accMagnitude.assign ((size_t) sourceSamples, 0.0f);

        for (int chan : s->movementChannels)
        {
            const float* axis = buffer.getReadPointer (chan, 0);
            for (int i = 0; i < sourceSamples; i++)
                s->accMagnitude[(size_t) i] += axis[i] * axis[i];
        }

        for (int i = 0; i < sourceSamples; i++)
            s->accMagnitude[(size_t) i] = std::sqrt (s->accMagnitude[(size_t) i]);

        movData = s->accMagnitude.data();
    }
    else // EMG
    {
        movData = buffer.getReadPointer (s->movementChannels[0], 0);
    }

    const int window = std::max (1, std::min (s->params.rmsSamples, sourceSamples));

    s->movRmsValues.clear();
    s->movRmsNumSamples.clear();

    for (int start = 0; start < sourceSamples; start += window)
    {
        const int end = std::min (start + window, sourceSamples);
        const double rms = calculateRms (movData, start, end);

        if (s->isCalibrating)
        {
            s->movBaseline.accumulate (rms);
        }
        else
        {
            s->movRmsValues.push_back (rms);
            s->movRmsNumSamples.push_back (std::max (1, (int) std::lround ((end - start) * toThisStream)));
        }
    }

    if (! s->isCalibrating)
        evalMovement (streamId, firstSample, numSamples);
}

// Calculate the RMS of data from position initIndex (included) to endIndex (not included)
double RippleDetector::calculateRms (const float* data, int initIndex, int endIndex)
{
    double sum = 0.0;
    for (int idx = initIndex; idx < endIndex; idx++)
        sum += (double) data[idx] * (double) data[idx];

    return std::sqrt (sum / (double) (endIndex - initIndex));
}

// Print calculated statistics
void RippleDetector::logCalibration (uint16 streamId)
{
    RippleDetectorSettings* s = settings[streamId];

    LOGC ("Calibration finished for stream ", streamId);
    LOGC ("Ripple channel -> RMS baseline mean: ", s->method->getBaselineMean());
    LOGC ("Ripple channel -> RMS baseline std: ", s->method->getBaselineStd());
    LOGC ("Ripple channel -> threshold (mean + ", s->params.onsetSds, " SD): ", s->method->getOnsetThreshold());

    if (s->params.adaptiveBaseline)
        LOGC ("Ripple channel -> adaptive baseline, tau = ", s->params.adaptTauSeconds, " s");

    if (s->movSwitchEnabled)
    {
        String movSwitchStr = s->movSwitch.equalsIgnoreCase ("EMG") ? "EMG" : "Accel. Magnit.";

        LOGC (movSwitchStr, " RMS mean: ", s->movBaseline.getMean());
        LOGC (movSwitchStr, " RMS std: ", s->movBaseline.getStd());
        LOGC (movSwitchStr, " threshold amplifier: ", s->movSds);
        LOGC (movSwitchStr, " final RMS threshold: ", s->getMovThreshold());
    }
}

// Evaluate EMG/ACC signal to enable or disable ripple detection
void RippleDetector::evalMovement (uint16 streamId, int64 firstSample, int numSamples)
{
    RippleDetectorSettings* s = settings[streamId];
    const double movThreshold = s->getMovThreshold();

    int sampleOffset = 0;

    // Iterate over RMS blocks inside buffer
    for (size_t rmsIdx = 0; rmsIdx < s->movRmsValues.size(); rmsIdx++)
    {
        const double rms = s->movRmsValues[rmsIdx];
        const int samples = s->movRmsNumSamples[rmsIdx];

        // Counter: accumulate time above or below threshold
        if (rms > movThreshold)
        {
            s->counterMovUpThresh += samples;
            s->flagMovMinTimeDown = false;
        }
        else
        {
            s->counterMovDownThresh += samples;
            s->flagMovMinTimeUp = false;
            s->counterMovUpThresh = 0;
        }

        // Set flags when minimum time above or below threshold is achieved
        if (s->counterMovUpThresh > s->minMovSamplesAboveThresh)
        {
            s->flagMovMinTimeUp = true;
            s->counterMovDownThresh = 0; //Reset counterMovDownThresh only when there is movement for enough time
        }
        if (s->counterMovDownThresh > s->minMovSamplesBelowThresh)
        {
            s->flagMovMinTimeDown = true;
        }

        // Another stream's windows are converted to this stream's samples; keep the events in this block
        const int at = std::min (sampleOffset, std::max (0, numSamples - 1));

        // Disable plugin...
        if (s->pluginEnabled && s->flagMovMinTimeUp)
        {
            s->pluginEnabled = false;
            addEvent (s->createEvent (s->movementOutputChannel, firstSample + at, true), at);

            // Do not leave the ripple line high while detection is blocked
            if (s->rippleTtlHigh)
                setRippleTtl (streamId, false, at, firstSample);
        }
        // ... or enable plugin
        if (! s->pluginEnabled && s->flagMovMinTimeDown)
        {
            s->pluginEnabled = true;
            addEvent (s->createEvent (s->movementOutputChannel, firstSample + at, false), at);
        }

        sampleOffset += samples;
    }
}
