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
    : VisualizerEditor (parentNode, "Ripple Detector", 605)
{
    rippleDetector = (RippleDetector*) parentNode;

    /* Column 1: channels and calibration */
    int col1 = 10;

    addSelectedChannelsParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "Ripple_Input", col1, 22);
    ParameterEditor* rippleInput = getParameterEditor ("Ripple_Input");
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

    /* Column 2: detection settings */
    int col2 = 98;

    addRow ("ripple_std", col2, ROW_Y[0]);
    addRow ("time_thresh", col2, ROW_Y[1]);
    addRow ("refr_time", col2, ROW_Y[2]);
    addRow ("rms_samples", col2, ROW_Y[3]);

    /* Column 3: baseline mode */
    int col3 = 255;

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
    laserTestButton->setTooltip ("Fires the laser once through the Pi's web API, whether or not Laser Trigger is on, "
                                 "and shows the answer: the round-trip time in ms when it fired, REJECTED (the Pi is up "
                                 "but did not fire), NO LINK (no answer from Laser Host:Laser Port) or SET HOST.");
    laserTestButton->setBounds (col3 + TEXT_WIDTH - 56, ROW_Y[2], 56, ROW_HEIGHT);
    addAndMakeVisible (laserTestButton.get());

    for (int i = 0; i < 2; i++)
    {
        const String name = i == 0 ? "laser_host" : "laser_port";
        addTextBoxParameterEditor (Parameter::PROCESSOR_SCOPE, name, col3, ROW_Y[3 + i]);
        ParameterEditor* ed = getParameterEditor (name);
        ed->setLayout (ParameterEditor::Layout::nameOnLeft);
        ed->setSize (TEXT_WIDTH, ROW_HEIGHT);
    }

    /* Column 4 and 5: EMG / ACC movement detection settings */
    int col4 = 412;

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

    int col5 = 507;

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

    switch (trigger.getTestReply())
    {
        case LaserTriggerHttp::Reply::Fired:
            showLaserTestLabel (String (trigger.getTestMs(), 0) + " ms", 30);
            break;
        case LaserTriggerHttp::Reply::Rejected:
            showLaserTestLabel ("REJECTED", 30);
            break;
        default:
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

    calibrateButton->setLabel (calibrateButtonText (rippleDetector, streamId, acquisitionIsActive));
    calibrateButton->setEnabledState (! calibrating);
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
    // Adaptive baseline time constant
    bool adaptive = false;
    if (auto* stream = rippleDetector->getDataStream (getCurrentStream()))
        if (auto* p = stream->getParameter ("baseline"))
            adaptive = p->getValueAsString().equalsIgnoreCase ("Adaptive");

    if (auto* ed = getParameterEditor ("adapt_tau"))
        ed->setVisible (adaptive);

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
}
