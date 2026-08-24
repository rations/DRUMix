// The editor's painting, with no VST3 and no X11 in its include graph.
//
// Everything the panel draws is a function of PanelState, which holds plain
// types only. That is what lets the offline render tool paint exactly what the
// plug-in paints — same Canvas, same art, same geometry — with no host, no
// window and no SDK objects, so the layout can be reviewed from a PNG.
//
// DrumEditorView fills a PanelState from the controller on every frame and
// calls drawPanel(); it keeps the interaction, the parameter edits and the
// window to itself. The control rectangles below are shared by the drawing and
// the hit-testing so the two can never disagree.
//
// The same applies to the kit rack page (drawRack / RackState), which the
// editor draws INSTEAD of the kit panel while it is open. It is a page rather
// than a second window on purpose: a plug-in is given one window by the host
// and painting a page inside it needs no extra window lifecycle, no second
// run-loop registration and no toolkit.

#pragma once

#include "drumgeometry.h"
#include "gfx/canvas.h"
#include "gfx/image.h"

#include <string>
#include <vector>

namespace DRUMix
{

//------------------------------------------------------------------------
struct PanelState {
    int selectedPad = 0; // index into geo::kPads
    int armedSlot = -1;  // slot waiting for a MIDI-learn note, or -1
    // Strip button held down, or -1. geo::kButtonCount means the Expand kit
    // button, which sits past the end of geo::kButtons.
    int pressedButton = -1;
    bool sampleLoaded = false;
    std::string sampleName;              // file name only, empty when nothing is loaded
    int note = -1;                       // bound MIDI note, or -1 for unassigned
    double volume = 0.8;                 // 0..1, the selected slot's
    int pulsePhase = 0;                  // advances once per tick; drives the learn pulse
    float hitLevel[geo::kPadCount] = {}; // pad flash, 0..1
};

// Paint the whole panel. `images` is the caller's cache, so the art is loaded
// once per editor rather than once per frame.
void drawPanel(Canvas &c, ImageCache &images, const PanelState &state);

//------------------------------------------------------------------------
// Control rectangles — one definition, used by both the drawing and the
// hit-testing.
inline Rect buttonRect(int index)
{
    const geo::ButtonSpec &spec = geo::kButtons[index];
    return Rect(static_cast<float>(spec.x), static_cast<float>(spec.y), static_cast<float>(spec.w),
                static_cast<float>(spec.h));
}

inline Rect volumeHitRect()
{
    return Rect(static_cast<float>(geo::kVolumeHitX), static_cast<float>(geo::kVolumeHitY),
                static_cast<float>(geo::kVolumeHitW), static_cast<float>(geo::kVolumeHitH));
}

// Normalised volume for a pointer x inside (or beside) the track, clamped.
double volumeForX(float x);

//------------------------------------------------------------------------
// The kit rack page: every shown slot as a row, so the slots the nine pads do
// not cover can still be loaded, bound and mixed.
//------------------------------------------------------------------------

// One rack row, already resolved by the editor. `name` is the kit pad's name
// for the slots a pad drives and null for the rest, which are labelled by
// number.
struct RackRow {
    int slot = 0;
    const char *name = nullptr;
    bool sampleLoaded = false;
    std::string sampleName; // file name only
    int note = -1;          // bound MIDI note, or -1 for unassigned
    double volume = 0.8;
    bool armed = false;    // waiting for a MIDI-learn note
    float hitLevel = 0.0f; // trigger flash, 0..1
};

// Which of the page's own (non-row) controls is held down.
enum RackControl { kRackNothing = -1, kRackClose = 0, kRackAdd = 1, kRackRemove = 2 };

struct RackState {
    int firstRow = 0;  // slot index of the first visible row
    int slotCount = 1; // rows the rack shows in total, 1 .. maxSlots
    int maxSlots = 1;
    int selectedSlot = -1; // the kit page's selected slot, marked in the list
    int pulsePhase = 0;
    int pressedRow = -1;    // slot whose row button is held, or -1
    int pressedButton = -1; // index into geo::kRackButtons
    RackControl pressedControl = kRackNothing;
    std::vector<RackRow> rows; // the visible rows, starting at firstRow
};

// `rows` is expected to hold the visible window only; anything past
// geo::kRackVisibleRows is not drawn.
void drawRack(Canvas &c, const RackState &state);

//------------------------------------------------------------------------
// Rack control rectangles — again one definition for drawing and hit-testing.
// `row` is a position on screen (0 .. geo::kRackVisibleRows - 1), not a slot.
inline Rect rackRowRect(int row)
{
    return Rect(static_cast<float>(geo::kRackRowLeft),
                static_cast<float>(geo::kRackRowsY + row * geo::kRackRowH),
                static_cast<float>(geo::kRackRowRight - geo::kRackRowLeft),
                static_cast<float>(geo::kRackRowH));
}

inline Rect rackButtonRect(int row, int index)
{
    const geo::RackButtonSpec &spec = geo::kRackButtons[index];
    return Rect(static_cast<float>(spec.x),
                static_cast<float>(geo::kRackRowsY + row * geo::kRackRowH + geo::kRackButtonInset),
                static_cast<float>(spec.w), static_cast<float>(geo::kRackButtonH));
}

// A little taller than the track, so the handle is easy to grab.
inline Rect rackVolumeHitRect(int row)
{
    const Rect r = rackRowRect(row);
    return Rect(static_cast<float>(geo::kRackVolX - 6), r.y + 4.0f,
                static_cast<float>(geo::kRackVolW + 12), r.h - 8.0f);
}

inline Rect rackCloseBox()
{
    return Rect::fromLTRB(
        static_cast<float>(geo::kRackCloseL), static_cast<float>(geo::kRackCloseT),
        static_cast<float>(geo::kRackCloseR), static_cast<float>(geo::kRackCloseB));
}

inline Rect rackAddRect()
{
    return Rect(static_cast<float>(geo::kRackAddX), static_cast<float>(geo::kRackFooterY),
                static_cast<float>(geo::kRackAddW), static_cast<float>(geo::kRackFooterH));
}

inline Rect rackRemoveRect()
{
    return Rect(static_cast<float>(geo::kRackRemoveX), static_cast<float>(geo::kRackFooterY),
                static_cast<float>(geo::kRackRemoveW), static_cast<float>(geo::kRackFooterH));
}

inline Rect expandButtonRect()
{
    return Rect(static_cast<float>(geo::kExpandX), static_cast<float>(geo::kExpandY),
                static_cast<float>(geo::kExpandW), static_cast<float>(geo::kExpandH));
}

// Normalised volume for a pointer x over a rack row's track, clamped.
double rackVolumeForX(float x);

} // namespace DRUMix
