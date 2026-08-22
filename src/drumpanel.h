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

#pragma once

#include "drumgeometry.h"
#include "gfx/canvas.h"
#include "gfx/image.h"

#include <string>

namespace DRUMix
{

//------------------------------------------------------------------------
struct PanelState {
    int selectedPad = 0;    // index into geo::kPads
    int armedSlot = -1;     // slot waiting for a MIDI-learn note, or -1
    int pressedButton = -1; // strip button held down, or -1
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

} // namespace DRUMix
