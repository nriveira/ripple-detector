#include "RippleDetectorEditor.h"
#include "RippleDetectorCanvas.h"

// Row grid for the text-box style editors
static const int ROW_HEIGHT = 17;
static const int ROW_PITCH = 21;
static const int ROW_Y[5] = { 24, 24 + ROW_PITCH, 24 + 2 * ROW_PITCH, 24 + 3 * ROW_PITCH, 24 + 4 * ROW_PITCH };
static const int TEXT_WIDTH = 150;

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
    calibrateButton->setBounds (col1, 92, 80, 16);
    addAndMakeVisible (calibrateButton.get());

    // Custom toggle for the "feature_out" parameter: a ToggleParameterEditor does not fit in this column
    featureToggle = std::make_unique<ToggleButton> ("Features");
    featureToggle->setTooltip ("Add the detection feature and thresholds to the stream as continuous channels (RIP_FEAT, RIP_ON, RIP_OFF), e.g. for recording. Rebuilds the signal chain. Not needed for the built-in viewer.");
    featureToggle->addListener (this);
    featureToggle->setBounds (col1 - 2, 109, 86, 16);
    addAndMakeVisible (featureToggle.get());

    /* Column 2: method and shared detection settings */
    int col2 = 98;

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "method", col2, ROW_Y[0]);
    ParameterEditor* method = getParameterEditor ("method");
    method->setLayout (ParameterEditor::Layout::nameOnLeft);
    method->setSize (TEXT_WIDTH, ROW_HEIGHT);

    addRow ("ripple_std", col2, ROW_Y[1]);
    addRow ("time_thresh", col2, ROW_Y[2]);
    addRow ("refr_time", col2, ROW_Y[3]);
    addRow ("rms_samples", col2, ROW_Y[4]);

    /* Column 3: method-specific settings and baseline mode */
    int col3 = 255;

    addRow ("smooth_ms", col3, ROW_Y[0]);
    addRow ("ripple_std_off", col3, ROW_Y[1]);
    addRow ("max_dur", col3, ROW_Y[2]);

    addComboBoxParameterEditor (Parameter::ParameterScope::STREAM_SCOPE, "baseline", col3, ROW_Y[3]);
    ParameterEditor* baseline = getParameterEditor ("baseline");
    baseline->setLayout (ParameterEditor::Layout::nameOnLeft);
    baseline->setSize (TEXT_WIDTH, ROW_HEIGHT);

    addRow ("adapt_tau", col3, ROW_Y[4]);

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

void RippleDetectorEditor::buttonClicked (Button* button)
{
    if (button == calibrateButton.get())
    {
        rippleDetector->shouldCalibrate = true;
    }
    else if (button == featureToggle.get())
    {
        if (auto* stream = rippleDetector->getDataStream (getCurrentStream()))
            if (auto* p = stream->getParameter ("feature_out"))
                p->setNextValue (featureToggle->getToggleState());
    }
}

void RippleDetectorEditor::startAcquisition()
{
    featureToggle->setEnabled (false);
    enable();
}

void RippleDetectorEditor::stopAcquisition()
{
    featureToggle->setEnabled (true);
    disable();
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
    const String method = rippleDetector->getMethodName (getCurrentStream());
    const bool usesHysteresis = ! method.equalsIgnoreCase ("RMS");

    // Envelope / TKEO parameters
    for (auto name : { "smooth_ms", "ripple_std_off", "max_dur" })
    {
        if (auto* ed = getParameterEditor (name))
            ed->setVisible (usesHysteresis);
    }

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
}
