#include "RippleDetectorEditor.h"
#include "RippleDetectorCanvas.h"

// Row grid for the text-box style editors
static const int ROW_HEIGHT = 17;
static const int ROW_PITCH = 21;
static const int ROW_Y[5] = { 24, 24 + ROW_PITCH, 24 + 2 * ROW_PITCH, 24 + 3 * ROW_PITCH, 24 + 4 * ROW_PITCH };
static const int TEXT_WIDTH = 150;

static const String CALIBRATE_TOOLTIP =
    "Estimates the baseline of the ripple channel: for 20 s the RMS of every window is collected and its mean and "
    "standard deviation are computed. The detection threshold is mean + Onset Std Dev x SD. No ripples are reported "
    "while calibrating. Calibration runs automatically when acquisition starts and when the movement channels change; "
    "press to run it again, e.g. after moving the electrode or changing the filter.";

// Class constructor
RippleDetectorEditor::RippleDetectorEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "Ripple Detector", 693)
{
    rippleDetector = (RippleDetector*) parentNode;

    /* Column 1: channels and calibration */
    int col1 = 10;

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "ripple_channel", col1, 22);
    ParameterEditor* rippleInput = getParameterEditor ("ripple_channel");
    rippleInput->setLayout (ParameterEditor::Layout::nameOnTop);
    rippleInput->setSize (80, 34);

    addTtlLineParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "Ripple_Out", col1, 57);
    ParameterEditor* rippleOut = getParameterEditor ("Ripple_Out");
    rippleOut->setLayout (ParameterEditor::Layout::nameOnTop);
    rippleOut->setSize (80, 34);

    calibrateButton = std::make_unique<UtilityButton> ("CALIBRATE");
    calibrateButton->addListener (this);
    calibrateButton->setRadius (3.0f);
    calibrateButton->setTooltip (CALIBRATE_TOOLTIP);
    calibrateButton->setBounds (col1, 92, 80, 16);
    addAndMakeVisible (calibrateButton.get());

    // Custom toggle for the "feature_out" parameter: a ToggleParameterEditor does not fit in this column
    featureToggle = std::make_unique<ToggleButton> ("Features");
    featureToggle->setTooltip ("Add the RMS feature, the detection threshold and a detected-events pulse to the stream as continuous channels (RIP_FEAT, RIP_THR, RIP_EVENT), e.g. for the LFP Viewer or for recording. Rebuilds the signal chain.");
    featureToggle->addListener (this);
    featureToggle->setBounds (col1 - 2, 109, 86, 16);
    addAndMakeVisible (featureToggle.get());

    /* Noise channel: onsets that also cross threshold here are vetoed */
    int colNoise = 98;

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "noise_channel", colNoise, 22);
    ParameterEditor* noiseInput = getParameterEditor ("noise_channel");
    noiseInput->setLayout (ParameterEditor::Layout::nameOnTop);
    noiseInput->setSize (80, 34);

    // Hardware trigger of the stimulus controller, for the measured latency
    addTtlLineParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "stim_in", colNoise, 57);
    ParameterEditor* stimIn = getParameterEditor ("stim_in");
    stimIn->setLayout (ParameterEditor::Layout::nameOnTop);
    stimIn->setSize (80, 34);

    /* Column 2: detection settings */
    int col2 = 186;

    addRow ("ripple_std", col2, ROW_Y[0]);
    addRow ("time_thresh", col2, ROW_Y[1]);
    addRow ("refr_time", col2, ROW_Y[2]);
    addRow ("rms_samples", col2, ROW_Y[3]);

    // Pulse test mode: its settings take the place of the ripple settings (updateMethodView)
    addRow ("pulse_thresh", col2, ROW_Y[0]);
    addRow ("pulse_lockout", col2, ROW_Y[1]);
    addRow ("resp_thresh", col2, ROW_Y[2]);

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "detect_mode", col2, ROW_Y[4]);
    ParameterEditor* detectMode = getParameterEditor ("detect_mode");
    detectMode->setLayout (ParameterEditor::Layout::nameOnLeft);
    detectMode->setSize (TEXT_WIDTH, ROW_HEIGHT);

    /* Column 3: baseline mode */
    int col3 = 343;

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "baseline", col3, ROW_Y[0]);
    ParameterEditor* baseline = getParameterEditor ("baseline");
    baseline->setLayout (ParameterEditor::Layout::nameOnLeft);
    baseline->setSize (TEXT_WIDTH, ROW_HEIGHT);

    addRow ("adapt_tau", col3, ROW_Y[1]);

    // Closed-loop laser trigger (applies to every stream)
    addToggleParameterEditor (Parameter::PROCESSOR_SCOPE, "laser_trigger", col3, ROW_Y[2]);
    ParameterEditor* laserTrigger = getParameterEditor ("laser_trigger");
    laserTrigger->setLayout (ParameterEditor::Layout::nameOnLeft);
    laserTrigger->setSize (TEXT_WIDTH - 58, ROW_HEIGHT);

    laserTestButton = std::make_unique<UtilityButton> ("TEST");
    laserTestButton->addListener (this);
    laserTestButton->setRadius (3.0f);
    laserTestButton->setTooltip ("Fires the laser once, whether or not Laser Trigger is on, and shows the answer: the "
                                 "round trip in ms when it fired (UDP; with HTTP, the time to the Pi's reply, which it sends "
                                 "after firing), BUSY (the laser was mid-pulse and ignores the trigger), NO MCU (the Pi has "
                                 "not heard from the laser controller), REJECTED (the Pi answered but did not fire), NO LINK "
                                 "(no answer from Laser Host and Port) or SET HOST.");
    laserTestButton->setBounds (col3 + TEXT_WIDTH - 56, ROW_Y[2], 56, ROW_HEIGHT);
    addAndMakeVisible (laserTestButton.get());

    addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "laser_host", col3, ROW_Y[3]);
    ParameterEditor* laserHost = getParameterEditor ("laser_host");
    laserHost->setLayout (ParameterEditor::Layout::nameOnLeft);
    laserHost->setSize (TEXT_WIDTH, ROW_HEIGHT);

    // Transport, then the port for it (updateMethodView shows one of the two)
    addComboBoxParameterEditor (Parameter::PROCESSOR_SCOPE, "laser_transport", col3, ROW_Y[4]);
    ParameterEditor* laserVia = getParameterEditor ("laser_transport");
    laserVia->setLayout (ParameterEditor::Layout::nameHidden);
    laserVia->setSize (58, ROW_HEIGHT);

    for (auto* name : { "laser_udp_port", "laser_port" })
    {
        addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, name, col3 + 62, ROW_Y[4]);
        ParameterEditor* ed = getParameterEditor (name);
        ed->setLayout (ParameterEditor::Layout::nameOnLeft);
        ed->setSize (TEXT_WIDTH - 62, ROW_HEIGHT);
    }

    /* Columns 4 and 5: EMG / ACC movement detection, anchored to the right edge.
       Mode, input and output sit in the rightmost column so the block stays flush
       right when movement detection is off and only mode and input are shown. */
    int col4 = 595;

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "mov_detect", col4, 22);
    ParameterEditor* movDetect = getParameterEditor ("mov_detect");
    movDetect->setLayout (ParameterEditor::Layout::nameOnTop);
    movDetect->setSize (90, 34);

    addSelectedChannelsParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "Mov_Input", col4, 57);
    ParameterEditor* movInput = getParameterEditor ("Mov_Input");
    movInput->setLayout (ParameterEditor::Layout::nameOnTop);
    movInput->setSize (90, 34);

    addTtlLineParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "Mov_Out", col4, 92);
    ParameterEditor* movOut = getParameterEditor ("Mov_Out");
    movOut->setLayout (ParameterEditor::Layout::nameOnTop);
    movOut->setSize (90, 34);

    int col5 = 500;

    addTextBoxParameterEditor (Parameter::STREAM_SCOPE, "mov_std", col5, 22);
    ParameterEditor* movStd = getParameterEditor ("mov_std");
    movStd->setLayout (ParameterEditor::Layout::nameOnTop);
    movStd->setSize (90, 34);

    addTextBoxParameterEditor (Parameter::STREAM_SCOPE, "min_time_st", col5, 57);
    ParameterEditor* minTimeSt = getParameterEditor ("min_time_st");
    minTimeSt->setLayout (ParameterEditor::Layout::nameOnTop);
    minTimeSt->setSize (90, 34);

    addTextBoxParameterEditor (Parameter::STREAM_SCOPE, "min_time_mov", col5, 92);
    ParameterEditor* minTimeMov = getParameterEditor ("min_time_mov");
    minTimeMov->setLayout (ParameterEditor::Layout::nameOnTop);
    minTimeMov->setSize (90, 34);

    updateMethodView();
}

void RippleDetectorEditor::addRow (const String& name, int x, int y)
{
    addTextBoxParameterEditor (Parameter::STREAM_SCOPE, name, x, y);
    ParameterEditor* ed = getParameterEditor (name);
    ed->setLayout (ParameterEditor::Layout::nameOnLeft);
    ed->setSize (TEXT_WIDTH, ROW_HEIGHT);
}

Visualizer* RippleDetectorEditor::createNewCanvas()
{
    return new RippleDetectorCanvas (rippleDetector);
}

String RippleDetectorEditor::calibrateButtonText (RippleDetector* processor, uint16 streamId, bool acquiring)
{
    if (acquiring && processor->isCalibrating (streamId))
    {
        const int percent = (int) std::round (100.0f * std::max (0.0f, processor->getCalibrationProgress (streamId)));
        return "CALIB. " + String (percent) + "%";
    }

    return "CALIBRATE";
}

void RippleDetectorEditor::buttonClicked (Button* button)
{
    if (button == calibrateButton.get())
    {
        rippleDetector->shouldCalibrate = true;
        timerCallback();
    }
    else if (button == laserTestButton.get())
    {
        startLaserTest();
    }
    else if (button == featureToggle.get())
    {
        if (auto* stream = rippleDetector->getDataStream (getCurrentStream()))
            if (auto* p = stream->getParameter ("feature_out"))
                p->setNextValue (featureToggle->getToggleState());
    }
}

void RippleDetectorEditor::startLaserTest()
{
    LaserTrigger& trigger = rippleDetector->getLaserTrigger();

    if (! trigger.test())
    {
        showLaserTestLabel ("SET HOST", 10);
        return;
    }

    laserTestTarget = trigger.getTestsRequested();
    laserTestResultTicks = 0;
    laserTestButton->setLabel ("...");
    laserTestButton->setEnabledState (false);
    laserTestPoller.startTimer (100);
}

void RippleDetectorEditor::pollLaserTest()
{
    // Counting down a shown result
    if (laserTestResultTicks > 0)
    {
        if (--laserTestResultTicks == 0)
        {
            laserTestPoller.stopTimer();
            laserTestButton->setLabel ("TEST");
        }
        return;
    }

    LaserTrigger& trigger = rippleDetector->getLaserTrigger();
    if ((int32_t) (trigger.getTestsCompleted() - laserTestTarget) < 0)
        return; // still waiting for the Pi

    laserTestButton->setEnabledState (true);

    switch (trigger.getTestOutcome())
    {
        case LaserTrigger::Outcome::Fired:
        {
            const double ms = trigger.getTestMs();
            showLaserTestLabel (String (ms, ms < 10.0 ? 1 : 0) + " ms", 30);
            break;
        }
        case LaserTrigger::Outcome::Busy:
            showLaserTestLabel ("BUSY", 30);
            break;
        case LaserTrigger::Outcome::McuDown:
            showLaserTestLabel ("NO MCU", 30);
            break;
        case LaserTrigger::Outcome::Rejected:
            showLaserTestLabel ("REJECTED", 30);
            break;
        case LaserTrigger::Outcome::NoLink:
            showLaserTestLabel ("NO LINK", 30);
            break;
    }
}

void RippleDetectorEditor::showLaserTestLabel (const String& text, int holdTicks)
{
    laserTestButton->setLabel (text);
    laserTestButton->setEnabledState (true);
    laserTestResultTicks = holdTicks;
    laserTestPoller.startTimer (100);
}

void RippleDetectorEditor::startAcquisition()
{
    featureToggle->setEnabled (false);
    startTimer (200);
    enable();
}

void RippleDetectorEditor::stopAcquisition()
{
    stopTimer();
    featureToggle->setEnabled (true);
    calibrateButton->setLabel ("CALIBRATE");
    calibrateButton->setEnabledState (true);
    disable();
}

void RippleDetectorEditor::timerCallback()
{
    const uint16 streamId = getCurrentStream();
    const bool calibrating = acquisitionIsActive && rippleDetector->isCalibrating (streamId);
    const bool pulse = rippleDetector->isPulseMode (streamId);

    // Pulse test mode has no baseline to calibrate
    calibrateButton->setLabel (pulse ? String ("CALIBRATE") : calibrateButtonText (rippleDetector, streamId, acquisitionIsActive));
    calibrateButton->setEnabledState (! calibrating && ! pulse);
}

// Called when settings are updated
void RippleDetectorEditor::updateSettings()
{
    updateMethodView();
}

void RippleDetectorEditor::selectedStreamHasChanged()
{
    updateMethodView();

    // The viewer follows the stream selected in the editor
    if (canvas != nullptr)
        canvas->update();
}

void RippleDetectorEditor::updateMethodView()
{
    // Pulse test mode replaces the ripple settings with its own; the noise channel is bypassed
    const bool pulse = rippleDetector->isPulseMode (getCurrentStream());

    for (auto* name : { "ripple_std", "time_thresh", "refr_time", "rms_samples", "baseline", "noise_channel" })
        if (auto* ed = getParameterEditor (name))
            ed->setVisible (! pulse);

    for (auto* name : { "pulse_thresh", "pulse_lockout", "resp_thresh" })
        if (auto* ed = getParameterEditor (name))
            ed->setVisible (pulse);

    // Adaptive baseline time constant
    bool adaptive = false;
    if (auto* stream = rippleDetector->getDataStream (getCurrentStream()))
        if (auto* p = stream->getParameter ("baseline"))
            adaptive = p->getValueAsString().equalsIgnoreCase ("Adaptive");

    if (auto* ed = getParameterEditor ("adapt_tau"))
        ed->setVisible (adaptive && ! pulse);

    // Only the port of the chosen laser transport
    const bool http = rippleDetector->getParameter ("laser_transport")->getValueAsString().equalsIgnoreCase ("HTTP");
    if (auto* ed = getParameterEditor ("laser_udp_port"))
        ed->setVisible (! http);
    if (auto* ed = getParameterEditor ("laser_port"))
        ed->setVisible (http);

    // Movement settings only matter while movement detection is on. Mov. Input
    // stays visible: channels must be chosen before ACC / EMG can be selected.
    bool movement = false;
    if (auto* stream = rippleDetector->getDataStream (getCurrentStream()))
        if (auto* p = stream->getParameter ("mov_detect"))
            movement = ! p->getValueAsString().equalsIgnoreCase ("OFF");

    for (auto* name : { "Mov_Out", "mov_std", "min_time_st", "min_time_mov" })
        if (auto* ed = getParameterEditor (name))
            ed->setVisible (movement);

    // The channel drop-downs' entries are the stream's channel names, set in the processor's updateSettings()
    for (auto* name : { "ripple_channel", "noise_channel" })
        if (auto* ed = getParameterEditor (name))
            ed->updateView();

    // Feature output toggle reflects the selected stream
    bool featureOut = false;
    bool hasStream = false;
    if (auto* stream = rippleDetector->getDataStream (getCurrentStream()))
        if (auto* p = stream->getParameter ("feature_out"))
        {
            featureOut = (bool) p->getValue();
            hasStream = true;
        }

    featureToggle->setToggleState (featureOut, dontSendNotification);
    featureToggle->setEnabled (hasStream && ! acquisitionIsActive);

    timerCallback();

    // The viewer's settings panel follows the detection mode too. Only while the editor's
    // stream exists: GenericEditor::update() refreshes the editor's settings before it
    // switches to the new streams, and Visualizer::update() assumes the stream is there.
    // After the switch, selectedStreamHasChanged() and updateVisualizer() update the viewer.
    if (canvas != nullptr && rippleDetector->getDataStream (getCurrentStream()) != nullptr)
        canvas->update();
}
