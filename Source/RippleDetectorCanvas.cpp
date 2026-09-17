#include "RippleDetectorCanvas.h"
#include "DetectionMethods/DetectionMethodFactory.h"

#include <cfloat>
#include <cmath>

namespace
{
const int RANGE_OPTIONS[] = { 5, 10, 20, 50, 100, 200 }; // SD
const int WINDOW_OPTIONS[] = { 2, 5, 10, 30 }; // seconds
const float Y_MIN = -3.0f; // SD; features are non-negative so they rarely go below this

const Colour TRACE_COLOUR (0x50, 0xa8, 0xff);
const Colour ONSET_COLOUR (0xff, 0x50, 0x50);
const Colour OFFSET_COLOUR (0xff, 0xb0, 0x40);
const Colour EVENT_COLOUR (0x40, 0xd0, 0x70);
const Colour BLOCKED_COLOUR (0xff, 0x90, 0x20);
const Colour CALIBRATING_COLOUR (0x90, 0x90, 0x90);

/** Rounds a step to 1, 2 or 5 times a power of ten */
float niceStep (float rawStep)
{
    const float power = std::pow (10.0f, std::floor (std::log10 (rawStep)));
    const float m = rawStep / power;
    if (m < 1.5f)
        return power;
    if (m < 3.5f)
        return 2.0f * power;
    if (m < 7.5f)
        return 5.0f * power;
    return 10.0f * power;
}
} // namespace

// ---------------------------------------------------------------- StreamDisplay

void RippleDetectorCanvas::StreamDisplay::reset (float sampleRate)
{
    binSamples = std::max (1, (int) std::round (sampleRate * BIN_MS / 1000.0f));
    bins.assign ((size_t) (HISTORY_SECONDS * 1000 / BIN_MS), Bin());
    head = 0;
    count = 0;
    clearCurrent();
}

void RippleDetectorCanvas::StreamDisplay::clearCurrent()
{
    current.mn.fill (FLT_MAX);
    current.mx.fill (-FLT_MAX);
    current.flags = 0;
    samplesInBin = 0;
}

void RippleDetectorCanvas::StreamDisplay::accumulate (const float* const* values, const uint8_t* flags, int numSamples)
{
    for (int i = 0; i < numSamples; i++)
    {
        for (int f = 0; f < NUM_FEATURES; f++)
        {
            const float v = values[f][i];
            current.mn[(size_t) f] = std::min (current.mn[(size_t) f], v);
            current.mx[(size_t) f] = std::max (current.mx[(size_t) f], v);
        }
        current.flags |= flags[i];

        if (++samplesInBin >= binSamples)
            push();
    }
}

void RippleDetectorCanvas::StreamDisplay::push()
{
    if (bins.empty())
        return;

    bins[head] = current;
    head = (head + 1) % bins.size();
    count = std::min (count + 1, bins.size());
    clearCurrent();
}

const RippleDetectorCanvas::Bin* RippleDetectorCanvas::StreamDisplay::fromNewest (size_t age) const
{
    if (age >= count)
        return nullptr;

    const size_t index = (head + bins.size() - 1 - age) % bins.size();
    return &bins[index];
}

// ---------------------------------------------------------------- Canvas

RippleDetectorCanvas::RippleDetectorCanvas (RippleDetector* processor_) : Visualizer (processor_),
                                                                          processor (processor_)
{
    refreshRate = 30; // Hz

    FontOptions labelFont ("Inter", "Regular", 14.0f);

    streamLabel = std::make_unique<Label> ("Stream", "No stream");
    streamLabel->setFont (FontOptions ("Inter", "Medium", 15.0f));
    addAndMakeVisible (streamLabel.get());

    featureLabel = std::make_unique<Label> ("FeatureLabel", "Feature:");
    featureLabel->setFont (labelFont);
    addAndMakeVisible (featureLabel.get());

    featureCombo = std::make_unique<ComboBox> ("Feature");
    featureCombo->addItem ("Active method", 1);
    int id = 2;
    for (const auto& name : getDetectionMethodNames())
        featureCombo->addItem (name, id++);
    featureCombo->setSelectedId (1, dontSendNotification);
    featureCombo->addListener (this);
    addAndMakeVisible (featureCombo.get());

    rangeLabel = std::make_unique<Label> ("RangeLabel", "Range:");
    rangeLabel->setFont (labelFont);
    addAndMakeVisible (rangeLabel.get());

    rangeCombo = std::make_unique<ComboBox> ("Range");
    for (int r : RANGE_OPTIONS)
        rangeCombo->addItem (String (r) + " SD", r);
    rangeCombo->setSelectedId (20, dontSendNotification);
    rangeCombo->addListener (this);
    addAndMakeVisible (rangeCombo.get());

    windowLabel = std::make_unique<Label> ("WindowLabel", "Window:");
    windowLabel->setFont (labelFont);
    addAndMakeVisible (windowLabel.get());

    windowCombo = std::make_unique<ComboBox> ("Window");
    for (int w : WINDOW_OPTIONS)
        windowCombo->addItem (String (w) + " s", w);
    windowCombo->setSelectedId (5, dontSendNotification);
    windowCombo->addListener (this);
    addAndMakeVisible (windowCombo.get());

    // Parameter panel: the same stream-scoped parameters as the editor
    panelTitle = std::make_unique<Label> ("PanelTitle", "Detection settings");
    panelTitle->setFont (FontOptions ("Inter", "Medium", 15.0f));
    addAndMakeVisible (panelTitle.get());

    addParameterRow ("method", true);
    addParameterRow ("ripple_std", false);
    addParameterRow ("ripple_std_off", false);
    addParameterRow ("time_thresh", false);
    addParameterRow ("refr_time", false);
    addParameterRow ("max_dur", false);
    addParameterRow ("smooth_ms", false);
    addParameterRow ("rms_samples", false);
    addParameterRow ("baseline", true);
    addParameterRow ("adapt_tau", false);

    calibrateButton = std::make_unique<UtilityButton> ("CALIBRATE");
    calibrateButton->setRadius (3.0f);
    calibrateButton->addListener (this);
    addAndMakeVisible (calibrateButton.get());
}

void RippleDetectorCanvas::addParameterRow (const String& name, bool comboBox)
{
    Parameter* param = processor->getStreamParameter (name);
    if (param == nullptr)
        return;

    ParameterEditor* editor;
    if (comboBox)
        editor = new ComboBoxParameterEditor (param, 20, PANEL_WIDTH - 30);
    else
        editor = new TextBoxParameterEditor (param, 20, PANEL_WIDTH - 30);

    editor->setLayout (ParameterEditor::Layout::nameOnLeft);
    addParameterEditor (editor, 0, 0); // positioned in resized()
}

uint16 RippleDetectorCanvas::currentStreamId() const
{
    if (processor->getEditor() == nullptr)
        return 0;

    return processor->getEditor()->getCurrentStream();
}

int RippleDetectorCanvas::displayedFeature() const
{
    const int selected = featureCombo->getSelectedId();

    if (selected >= 2)
        return std::min (NUM_FEATURES - 1, selected - 2);

    // "Active method": follow the stream's method parameter
    const String method = processor->getMethodName (currentStreamId());
    const Array<String> names = getDetectionMethodNames();
    for (int i = 0; i < names.size(); i++)
        if (names[i].equalsIgnoreCase (method))
            return std::min (NUM_FEATURES - 1, i);

    return 0;
}

void RippleDetectorCanvas::updateSettings()
{
    const uint16 streamId = currentStreamId();

    if (auto* stream = processor->getDataStream (streamId))
        streamLabel->setText (stream->getName() + "  (" + String (stream->getSampleRate(), 0) + " Hz)", dontSendNotification);
    else
        streamLabel->setText ("No stream", dontSendNotification);

    // Keep display rings only for streams that still exist
    std::map<uint16, StreamDisplay> kept;
    for (auto s : processor->getDataStreams())
    {
        auto it = displays.find (s->getStreamId());
        if (it != displays.end() && it->second.binSamples > 0 && ! it->second.bins.empty())
            kept[s->getStreamId()] = std::move (it->second);
        else
            kept[s->getStreamId()].reset (s->getSampleRate());
    }
    displays = std::move (kept);

    updateParameterVisibility();
    repaint();
}

void RippleDetectorCanvas::updateParameterVisibility()
{
    const String method = processor->getMethodName (currentStreamId());
    const bool usesHysteresis = ! method.equalsIgnoreCase ("RMS");

    for (auto name : { "smooth_ms", "ripple_std_off", "max_dur" })
        if (auto* ed = getParameterEditor (name))
            ed->setVisible (usesHysteresis);

    bool adaptive = false;
    if (auto* stream = processor->getDataStream (currentStreamId()))
        if (auto* p = stream->getParameter ("baseline"))
            adaptive = p->getValueAsString().equalsIgnoreCase ("Adaptive");

    if (auto* ed = getParameterEditor ("adapt_tau"))
        ed->setVisible (adaptive);

    resized();
}

void RippleDetectorCanvas::beginAnimation()
{
    // Discard whatever queued up while the viewer was not running
    pullData (true);
    startCallbacks();
}

void RippleDetectorCanvas::endAnimation()
{
    stopCallbacks();
}

void RippleDetectorCanvas::refresh()
{
    pullData (false);
    repaint (plotArea);
}

void RippleDetectorCanvas::refreshState()
{
    repaint();
}

void RippleDetectorCanvas::pullData (bool discard)
{
    for (auto stream : processor->getDataStreams())
    {
        const uint16 streamId = stream->getStreamId();
        FeatureFifo* fifo = processor->getFeatureFifo (streamId);
        if (fifo == nullptr)
            continue;

        auto it = displays.find (streamId);
        if (it == displays.end())
        {
            displays[streamId].reset (stream->getSampleRate());
            it = displays.find (streamId);
        }
        StreamDisplay& display = it->second;

        fifo->read ([&] (int start, int size)
                    {
                        if (discard)
                            return;

                        const float* values[NUM_FEATURES];
                        for (int f = 0; f < NUM_FEATURES; f++)
                            values[f] = fifo->getValues (f, start);

                        display.accumulate (values, fifo->getFlags (start), size);
                    });
    }
}

void RippleDetectorCanvas::resized()
{
    const int width = getWidth();
    const int height = getHeight();

    // Toolbar
    int x = 10;
    streamLabel->setBounds (x, 8, 260, 20);
    x += 270;
    featureLabel->setBounds (x, 8, 60, 20);
    featureCombo->setBounds (x + 60, 8, 130, 20);
    x += 200;
    rangeLabel->setBounds (x, 8, 55, 20);
    rangeCombo->setBounds (x + 55, 8, 80, 20);
    x += 145;
    windowLabel->setBounds (x, 8, 60, 20);
    windowCombo->setBounds (x + 60, 8, 70, 20);

    // Parameter panel
    const int panelX = width - PANEL_WIDTH + 15;
    int y = TOOLBAR_HEIGHT + 10;
    panelTitle->setBounds (panelX, y, PANEL_WIDTH - 30, 20);
    y += 28;

    for (auto* ed : getParameterEditors())
    {
        if (! ed->isVisible())
            continue;
        ed->setBounds (panelX, y, PANEL_WIDTH - 30, 20);
        y += 26;
    }

    calibrateButton->setBounds (panelX, y + 6, 100, 22);

    plotArea = Rectangle<int> (10, TOOLBAR_HEIGHT + 10, width - PANEL_WIDTH - 20, height - TOOLBAR_HEIGHT - 20);
}

void RippleDetectorCanvas::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentParentBackground));

    // Toolbar and panel backgrounds
    g.setColour (findColour (ThemeColours::componentBackground));
    g.fillRect (0, 0, getWidth(), TOOLBAR_HEIGHT);
    g.fillRect (getWidth() - PANEL_WIDTH, TOOLBAR_HEIGHT, PANEL_WIDTH, getHeight() - TOOLBAR_HEIGHT);

    drawPlot (g);
}

void RippleDetectorCanvas::drawPlot (Graphics& g)
{
    const int axisLeft = 44;
    const int axisBottom = 22;
    Rectangle<int> plot = plotArea.withTrimmedLeft (axisLeft).withTrimmedBottom (axisBottom);

    if (plot.getWidth() <= 0 || plot.getHeight() <= 0)
        return;

    g.setColour (findColour (ThemeColours::windowBackground));
    g.fillRect (plot);

    const float yMax = (float) rangeCombo->getSelectedId();
    const float yMin = Y_MIN;
    const int windowSeconds = windowCombo->getSelectedId();
    const size_t numBins = (size_t) (windowSeconds * 1000 / BIN_MS);
    const int feature = displayedFeature();

    auto yFor = [&] (float z)
    {
        const float t = (z - yMin) / (yMax - yMin);
        return (float) plot.getBottom() - t * (float) plot.getHeight();
    };

    // Status regions and trace
    const uint16 streamId = currentStreamId();
    auto it = displays.find (streamId);
    const StreamDisplay* display = it != displays.end() ? &it->second : nullptr;

    bool calibrating = false;
    bool blocked = false;

    if (display != nullptr && display->count > 0)
    {
        const int w = plot.getWidth();

        for (int px = 0; px < w; px++)
        {
            // Bins covered by this pixel column, oldest on the left
            const size_t b0 = (size_t) ((double) px * (double) numBins / (double) w);
            size_t b1 = (size_t) ((double) (px + 1) * (double) numBins / (double) w);
            if (b1 <= b0)
                b1 = b0 + 1;

            float mn = FLT_MAX;
            float mx = -FLT_MAX;
            uint8_t flags = 0;
            bool any = false;

            for (size_t b = b0; b < b1; b++)
            {
                const size_t age = numBins - 1 - b;
                const Bin* bin = display->fromNewest (age);
                if (bin == nullptr || bin->mn[(size_t) feature] > bin->mx[(size_t) feature])
                    continue;

                mn = std::min (mn, bin->mn[(size_t) feature]);
                mx = std::max (mx, bin->mx[(size_t) feature]);
                flags |= bin->flags;
                any = true;
            }

            if (! any)
                continue;

            const int x = plot.getX() + px;

            if (flags & FeatureFifo::CALIBRATING)
            {
                g.setColour (CALIBRATING_COLOUR.withAlpha (0.18f));
                g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
            }
            if (flags & FeatureFifo::BLOCKED)
            {
                g.setColour (BLOCKED_COLOUR.withAlpha (0.22f));
                g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
            }
            if (flags & FeatureFifo::TTL_HIGH)
            {
                g.setColour (EVENT_COLOUR.withAlpha (0.35f));
                g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
            }

            const float yTop = jlimit ((float) plot.getY(), (float) plot.getBottom(), yFor (mx));
            const float yBottom = jlimit ((float) plot.getY(), (float) plot.getBottom(), yFor (mn));

            g.setColour (TRACE_COLOUR);
            g.drawVerticalLine (x, yTop, std::max (yBottom, yTop + 1.0f));

            if (px == w - 1)
            {
                calibrating = (flags & FeatureFifo::CALIBRATING) != 0;
                blocked = (flags & FeatureFifo::BLOCKED) != 0;
            }
        }
    }

    // Threshold lines
    if (const DetectionParams* params = processor->getStreamParams (streamId))
    {
        const float onset = (float) params->onsetSds;
        const float offset = (float) std::min (params->offsetSds, params->onsetSds);

        g.setColour (ONSET_COLOUR);
        g.drawHorizontalLine ((int) yFor (onset), (float) plot.getX(), (float) plot.getRight());

        if (feature != 0) // RMS has no separate offset threshold
        {
            g.setColour (OFFSET_COLOUR);
            const float y = yFor (offset);
            for (int x = plot.getX(); x < plot.getRight(); x += 8)
                g.drawHorizontalLine ((int) y, (float) x, (float) std::min (x + 4, plot.getRight()));
        }
    }

    // Zero line
    g.setColour (findColour (ThemeColours::outline).withAlpha (0.6f));
    g.drawHorizontalLine ((int) yFor (0.0f), (float) plot.getX(), (float) plot.getRight());

    // Axes
    g.setColour (findColour (ThemeColours::outline));
    g.drawRect (plot);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions ("Inter", "Regular", 12.0f));

    const float step = niceStep ((yMax - yMin) / 5.0f);
    for (float z = std::ceil (yMin / step) * step; z <= yMax; z += step)
    {
        const int y = (int) yFor (z);
        g.drawText (String (z, 0), plotArea.getX(), y - 8, axisLeft - 6, 16, Justification::centredRight);
        g.drawHorizontalLine (y, (float) plot.getX(), (float) plot.getX() + 4);
    }

    const int timeTicks = 5;
    for (int i = 0; i <= timeTicks; i++)
    {
        const int x = plot.getX() + (int) ((float) plot.getWidth() * (float) i / (float) timeTicks);
        const float seconds = -(float) windowSeconds * (float) (timeTicks - i) / (float) timeTicks;
        g.drawText (String (seconds, 1) + " s", x - 30, plot.getBottom() + 4, 60, 16, Justification::centred);
        g.drawVerticalLine (x, (float) plot.getBottom() - 4, (float) plot.getBottom());
    }

    // Legend and status
    const Array<String> names = getDetectionMethodNames();
    String legend = names[std::min (feature, names.size() - 1)] + " (baseline SD)";
    g.setFont (FontOptions ("Inter", "Medium", 13.0f));
    g.setColour (TRACE_COLOUR);
    g.drawText (legend, plot.getX() + 8, plot.getY() + 4, 260, 18, Justification::centredLeft);

    if (calibrating)
    {
        g.setColour (CALIBRATING_COLOUR);
        g.drawText ("CALIBRATING", plot, Justification::centredTop);
    }
    else if (blocked)
    {
        g.setColour (BLOCKED_COLOUR);
        g.drawText ("BLOCKED BY MOVEMENT", plot, Justification::centredTop);
    }
}

void RippleDetectorCanvas::comboBoxChanged (ComboBox*)
{
    repaint (plotArea);
}

void RippleDetectorCanvas::buttonClicked (Button* button)
{
    if (button == calibrateButton.get())
        processor->shouldCalibrate = true;
}

void RippleDetectorCanvas::saveCustomParametersToXml (XmlElement* xml)
{
    xml->setAttribute ("feature", featureCombo->getSelectedId());
    xml->setAttribute ("range", rangeCombo->getSelectedId());
    xml->setAttribute ("window", windowCombo->getSelectedId());
}

void RippleDetectorCanvas::loadCustomParametersFromXml (XmlElement* xml)
{
    featureCombo->setSelectedId (xml->getIntAttribute ("feature", 1), dontSendNotification);
    rangeCombo->setSelectedId (xml->getIntAttribute ("range", 20), dontSendNotification);
    windowCombo->setSelectedId (xml->getIntAttribute ("window", 5), dontSendNotification);
}
