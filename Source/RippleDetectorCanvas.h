#ifndef __RIPPLE_DETECTOR_CANVAS_H
#define __RIPPLE_DETECTOR_CANVAS_H

#include <VisualizerWindowHeaders.h>

#include <array>
#include <map>
#include <vector>

#include "FeatureFifo.h"
#include "RippleDetector.h"

/**
    Live view of the ripple detection features.

    Shows, for the stream selected in the editor, the ripple channel as it enters the
    detector and, below it, the z-scored RMS feature together with the detection
    threshold, detected events, the movement-blocked state and the calibration progress.
    With a noise channel selected, its RMS is drawn against its own baseline and vetoed
    onsets are marked. The detection parameters of the stream can be edited from the
    panel on the right.
*/
class RippleDetectorCanvas : public Visualizer,
                             public ComboBox::Listener,
                             public Button::Listener
{
public:
    RippleDetectorCanvas (RippleDetector* processor);
    ~RippleDetectorCanvas() override {}

    /** Pulls new data from the processor and redraws */
    void refresh() override;

    /** Called when the tab becomes visible again */
    void refreshState() override;

    /** Called when the processor's settings or the selected stream change */
    void updateSettings() override;

    void beginAnimation() override;
    void endAnimation() override;

    void paint (Graphics& g) override;
    void resized() override;

    void comboBoxChanged (ComboBox* comboBox) override;
    void buttonClicked (Button* button) override;

    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

private:
    static constexpr int NUM_FEATURES = FeatureFifo::NUM_FEATURES;
    static constexpr int HISTORY_SECONDS = 60; // data kept per stream
    static constexpr int BIN_MS = 1; // display resolution
    static constexpr int PANEL_WIDTH = 230; // parameter panel on the right
    static constexpr int TOOLBAR_HEIGHT = 36;

    /** Min / max of every feature over one display bin, plus the OR of the status flags */
    struct Bin
    {
        std::array<float, NUM_FEATURES> mn;
        std::array<float, NUM_FEATURES> mx;
        uint8_t flags;
    };

    /** Ring buffer of display bins for one stream */
    struct StreamDisplay
    {
        std::vector<Bin> bins;
        size_t head { 0 };
        size_t count { 0 };
        Bin current;
        int samplesInBin { 0 };
        int binSamples { 1 };

        void reset (float sampleRate);
        void clearCurrent();
        void accumulate (const float* const* values, const uint8_t* flags, int numSamples);
        void push();

        /** Returns the bin 'age' bins before the newest, or nullptr */
        const Bin* fromNewest (size_t age) const;
    };

    uint16 currentStreamId() const;

    /** Drains the processor queues of every stream into the display rings */
    void pullData (bool discard);

    void drawPlot (Graphics& g);

    /** Draws the ripple channel's raw trace into 'area' */
    void drawRaw (Graphics& g, Rectangle<int> area, const StreamDisplay* display, size_t numBins);

    /** Min / max of one feature and the OR of the flags over the bins behind pixel column px; false if none */
    static bool columnStats (const StreamDisplay* display, size_t numBins, int width, int px, int feature,
                             float& mn, float& mx, uint8_t& flags);
    void addParameterRow (const String& name, bool comboBox);
    void updateParameterVisibility();

    RippleDetector* processor;
    std::map<uint16, StreamDisplay> displays;

    std::unique_ptr<Label> streamLabel;
    std::unique_ptr<Label> rangeLabel;
    std::unique_ptr<ComboBox> rangeCombo;
    std::unique_ptr<Label> windowLabel;
    std::unique_ptr<ComboBox> windowCombo;
    std::unique_ptr<Label> rawRangeLabel;
    std::unique_ptr<ComboBox> rawRangeCombo;
    std::unique_ptr<Label> panelTitle;
    std::unique_ptr<UtilityButton> calibrateButton;
    std::unique_ptr<Label> statsLabel;

    /** Updates the calibrate button text and the baseline read-out */
    void updateCalibrationInfo();

    Rectangle<int> plotArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RippleDetectorCanvas);
};

#endif
