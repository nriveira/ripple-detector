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

    /** The calibrate buttons double as Periodic (test)'s START / STOP: sets a button's label, state and tooltip */
    static void updateCalibrateButton (UtilityButton& button, RippleDetector* processor, uint16 streamId, bool acquiring);

    /** A calibrate button was pressed: calibrates, or starts / stops Periodic (test) */
    static void calibrateButtonPressed (RippleDetector* processor, uint16 streamId);

private:
    RippleDetector* rippleDetector;

    std::unique_ptr<UtilityButton> calibrateButton;
    std::unique_ptr<ToggleButton> featureToggle;
    std::unique_ptr<UtilityButton> laserTestButton;

    // Mov. Axes: several channels of another stream (e.g. the headstage IMU's), chosen in a pop-up
    std::unique_ptr<Label> movAxesLabel;
    std::unique_ptr<UtilityButton> movAxesButton;
    void showMovementAxesPopup();

    /** Polls the laser TEST request and shows its outcome on the button */
    class LaserTestPoller : public Timer
    {
    public:
        explicit LaserTestPoller (RippleDetectorEditor& e) : editor (e) {}
        void timerCallback() override { editor.pollLaserTest(); }

    private:
        RippleDetectorEditor& editor;
    };

    LaserTestPoller laserTestPoller { *this };
    uint32_t laserTestTarget { 0 }; // tests completed once this one is answered
    int laserTestResultTicks { 0 }; // > 0 while a result is shown

    void startLaserTest();
    void pollLaserTest();
    void showLaserTestLabel (const String& text, int holdTicks);

    /** Adds a text box editor with the name on the left, sized to one row of the grid */
    void addRow (const String& name, int x, int y);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RippleDetectorEditor);
};
#endif
