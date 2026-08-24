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
#include "idrumloader.h" // kMaxSlots
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
    void openBrowser(int slot, const char *what);
    void clearSample(int slot);
    void toggleLearn(int slot);
    void editVolume(int slot, double norm);
    void startVolumeDrag(int slot, double norm);
    void endVolumeDrag();

    //--- the rack page ---------------------------------------------------
    void openRack();
    void closeRack();
    void scrollRack(int rows);
    void setSlotCount(int count);
    int slotCount() const;
    // Screen position (0 .. geo::kRackVisibleRows - 1) under the pointer, or
    // -1; and the slot that row shows.
    int rackRowAt(float x, float y) const;
    int rackSlotAt(float x, float y) const;
    // Index into geo::kRackButtons for the button under the pointer, or -1.
    int rackButtonAt(int position, float x, float y) const;
    void onRackMouseDown(float x, float y);
    void onRackMouseUp(float x, float y);
    bool onRackMouseWheel(float x, float y, int delta);

    //--- helpers ---------------------------------------------------------
    int selectedSlot() const;
    int padForSlot(int slot) const;
    int padAt(float x, float y) const;
    int buttonAt(float x, float y) const; // index into geo::kButtons, or -1
    Steinberg::Vst::ParamID volumeParam(int slot) const;
    double paramValue(Steinberg::Vst::ParamID id) const;
    int slotNote(int slot) const; // bound MIDI note, or -1
    bool sampleFile(int slot, std::string &out) const;
    // Fill the controller-derived fields of whichever page is showing.
    void refreshPanel();
    void refreshRack();

    DrumController *mController = nullptr;

    FontStack mFonts;
    ImageCache mImages;
    bool mResourcesLoaded = false;

    FileBrowser mBrowser;
    int mBrowserSlot = 0; // slot the open browser will load into

    // Everything the kit page draws. The pulse phase lives here because it is
    // animation, not parameter state.
    PanelState mPanel;

    // The rack page, drawn instead of the kit page while it is open. It reaches
    // every slot, including the ones no pad drives.
    bool mRackOpen = false;
    RackState mRack;

    // Trigger flash level per slot, decayed on the tick. Kept for all slots
    // rather than for the nine pads, because the rack rows flash too.
    float mHit[kMaxSlots] = {};

    // Slot whose volume a drag is editing, or -1. One drag at a time, on
    // either page, so beginEdit/endEdit always pair up.
    int mDragSlot = -1;
};

} // namespace DRUMix
