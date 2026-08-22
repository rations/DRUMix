// DRUMix native editor (IPlugView / kPlatformTypeX11EmbedWindowID).
//
// DrumEditorView is the IPlugView the controller returns from createView(). It
// derives from X11PlugView, which owns the embedded X window, the run-loop
// registrations and the double buffer, and it paints through drawPanel(), the
// same SDK-free drawing code the offline render tool uses. What lives here is
// the part that cannot: the selection and animation state, the hit-testing,
// and the parameter edits.
//
// The controller pushes value changes in through ParamChanged(); user edits go
// out through EditController::beginEdit/performEdit/endEdit, which reach the
// host's IComponentHandler. All of it runs on the host's run-loop thread, the
// same thread the controller is called on, so none of it needs locking.

#pragma once

#include "drumpanel.h"
#include "filebrowser.h"
#include "gfx/fontstack.h"
#include "gfx/image.h"
#include "platform/x11plugview.h"

#include <string>

namespace DRUMix
{

class DrumController;

//------------------------------------------------------------------------
class DrumEditorView : public Steinberg::X11PlugView
{
public:
    explicit DrumEditorView(DrumController *controller);

    // Called by the controller whenever a parameter value changes (automation,
    // generic UI, state load, a pad trigger, a completed MIDI learn).
    void ParamChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

protected:
    //---from X11PlugView-------------
    void onAttached() SMTG_OVERRIDE;
    void onDraw(cairo_t *cr) SMTG_OVERRIDE;
    void onMouseDown(int x, int y, int button) SMTG_OVERRIDE;
    void onMouseUp(int x, int y, int button) SMTG_OVERRIDE;
    void onMouseMove(int x, int y) SMTG_OVERRIDE;
    void onMouseWheel(int x, int y, int delta) SMTG_OVERRIDE;
    void onTick() SMTG_OVERRIDE;

private:
    //--- interaction -----------------------------------------------------
    void selectPad(int pad);
    void openBrowser();
    void clearSample();
    void toggleLearn();
    void editVolume(double norm);

    //--- helpers ---------------------------------------------------------
    int selectedSlot() const;
    int padForSlot(int slot) const;
    int padAt(float x, float y) const;
    int buttonAt(float x, float y) const; // index into geo::kButtons, or -1
    Steinberg::Vst::ParamID volumeParam() const;
    double paramValue(Steinberg::Vst::ParamID id) const;
    bool sampleFile(int slot, std::string &out) const;
    // Fill mPanel's controller-derived fields for the selected slot.
    void refreshPanel();

    DrumController *mController = nullptr;

    FontStack mFonts;
    ImageCache mImages;
    bool mResourcesLoaded = false;

    FileBrowser mBrowser;
    int mBrowserSlot = 0; // slot the open browser will load into

    // Everything the panel draws. The pad flash levels and the pulse phase
    // live here because they are animation, not parameter state.
    PanelState mPanel;

    bool mVolumeDrag = false;
};

} // namespace DRUMix
