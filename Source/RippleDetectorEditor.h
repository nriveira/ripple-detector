#ifndef __RIPPLE_DETECTOR_EDITOR_H
#define __RIPPLE_DETECTOR_EDITOR_H

#include <EditorHeaders.h>

#include "RippleDetector.h"

class RippleDetectorEditor : public GenericEditor,
                             public Button::Listener
{
public:
    RippleDetectorEditor (GenericProcessor* parentNode);
    virtual ~RippleDetectorEditor() {}

    void buttonClicked (Button*) override;
    void updateSettings() override;
    void selectedStreamHasChanged() override;

    /** Shows only the parameters used by the selected stream's detection method */
    void updateMethodView();

private:
    RippleDetector* rippleDetector;

    std::unique_ptr<UtilityButton> calibrateButton;

    /** Adds a text box editor with the name on the left, sized to one row of the grid */
    void addRow (const String& name, int x, int y);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RippleDetectorEditor);
};
#endif
