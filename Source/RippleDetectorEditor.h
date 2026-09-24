#ifndef __RIPPLE_DETECTOR_EDITOR_H
#define __RIPPLE_DETECTOR_EDITOR_H

#include <VisualizerEditorHeaders.h>

#include "RippleDetector.h"

class RippleDetectorEditor : public VisualizerEditor,
                             public Button::Listener,
                             public Timer
{
public:
    RippleDetectorEditor (GenericProcessor* parentNode);
    virtual ~RippleDetectorEditor() {}

    /** Creates the feature viewer */
    Visualizer* createNewCanvas() override;

    void buttonClicked (Button*) override;
    void updateSettings() override;
    void selectedStreamHasChanged() override;
    void startAcquisition() override;
    void stopAcquisition() override;

    /** Polls the calibration state while acquiring */
    void timerCallback() override;

    /** Refreshes the custom controls for the selected stream */
    void updateMethodView();

    /** Text for a calibrate button given the stream's calibration state */
    static String calibrateButtonText (RippleDetector* processor, uint16 streamId, bool acquiring);

private:
    RippleDetector* rippleDetector;

    std::unique_ptr<UtilityButton> calibrateButton;
    std::unique_ptr<ToggleButton> featureToggle;

    /** Adds a text box editor with the name on the left, sized to one row of the grid */
    void addRow (const String& name, int x, int y);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RippleDetectorEditor);
};
#endif
