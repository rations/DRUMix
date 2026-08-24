// DRUMix editor geometry — mirrored BY HAND from gui/geometry.sh (the single
// source of truth for the art pipeline). Keep the two files in sync: every
// value here must match its shell counterpart, and the sprite base names must
// match what gui/make_pads.sh emits into resources/img/.
//
// That applies to the kit page, which is composited from generated sprites.
// The rack page below is drawn entirely from primitives and has no counterpart
// in the art pipeline, so its constants live here alone.

#pragma once

#include <cstdint>

namespace DRUMix
{
namespace geo
{

// Editor canvas (gui/geometry.sh: WIN_W/WIN_H).
constexpr int kWinW = 760;
constexpr int kWinH = 480;

// Control strip (STRIP_Y/STRIP_H).
constexpr int kStripY = 356;
constexpr int kStripH = 124;

// Transparent margin baked into every pad sprite (PAD_MARGIN): sprite side is
// 2 * (radius + kPadMargin), centered on the pad center.
constexpr int kPadMargin = 14;

// One pad of the kit. `sprite` selects the sprite family emitted by
// make_pads.sh (pad_<sprite>_{base,hit,sel,arm}.png); `slot` is the plug-in
// slot the pad drives (fixed mapping, matches the parameter layout).
struct PadSpec {
    int slot;
    bool hex; // hexagonal pad (true) or round cymbal pad (false)
    int x, y; // center, canvas coordinates
    int r;    // hex circumradius / circle radius
    const char *name;
    const char *sprite;
};

constexpr int kPadCount = 9;
constexpr PadSpec kPads[kPadCount] = {
    {0, true, 365, 245, 62, "Kick", "hex62"},     // KICK_*
    {1, true, 215, 245, 54, "Snare", "hex54"},    // SNARE_*
    {2, true, 295, 115, 48, "Tom 1", "hex48"},    // TOM1_*
    {3, true, 435, 115, 48, "Tom 2", "hex48"},    // TOM2_*
    {4, true, 520, 270, 54, "Floor Tom", "hex54"} // FLOOR_*
    ,
    {5, false, 88, 225, 46, "Hi-Hat", "cym46"},  // HIHAT_*
    {6, false, 105, 95, 52, "Crash 1", "cym52"}, // CRASH1_*
    {7, false, 585, 85, 52, "Crash 2", "cym52"}, // CRASH2_*
    {8, false, 650, 215, 58, "Ride", "cym58"},   // RIDE_*
};

// Palette (geometry.sh). 0xRRGGBB, as Canvas takes them.
constexpr uint32_t kBgTop = 0x14171C;      // kit backdrop, top of the gradient
constexpr uint32_t kStripBg = 0x0C0E11;    // control strip (STRIP_BG)
constexpr uint32_t kSeparator = 0x2F343B;  // strip rule, panel borders
constexpr uint32_t kPadBody = 0x181A1F;    // flat-pad fallback fill (PAD_BODY)
constexpr uint32_t kPadRim = 0x3A3F46;     // flat-pad fallback rim (PAD_RIM)
constexpr uint32_t kHitGlow = 0xFFD24A;    // HIT_GLOW
constexpr uint32_t kAccent = 0x4AA8FF;     // SEL_RING; also the browser accent
constexpr uint32_t kArmColor = 0xFF4A3A;   // ARM_RING, MIDI-learn
constexpr uint32_t kTextColor = 0xCDD2D7;  // strip labels
constexpr uint32_t kDimColor = 0x8C9298;   // secondary / unassigned text
constexpr uint32_t kTitleColor = 0xFFFFFF; // wordmark

// --- Control strip -------------------------------------------------------
// Transcribed from the Haiku editor's BRect layout, whose right/bottom edges
// are INCLUSIVE; the {x, y, w, h} below are the exclusive-edge equivalents
// (w = right - left, h = bottom - top), which is what Canvas::Rect wants.

// Label row: pad name, loaded file, bound note. `kLabelBaseline` is the text
// baseline, not the box top.
constexpr int kLabelBaseline = kStripY + 26;
constexpr int kPadLabelX = 16;
constexpr int kFileLabelX = 200;
constexpr int kNoteLabelX = 566;
constexpr int kStripRightPad = 16; // right margin for the note readout

// One clickable control in the strip.
struct ButtonSpec {
    int x, y, w, h;
    const char *label;
};
constexpr int kButtonY = kStripY + 40;
constexpr int kButtonH = 28;
constexpr int kButtonCount = 3;
enum ButtonIndex { kLoadButton = 0, kClearButton = 1, kLearnButton = 2 };
constexpr ButtonSpec kButtons[kButtonCount] = {
    {16, kButtonY, 90, kButtonH, "Load\u2026"},
    {114, kButtonY, 80, kButtonH, "Clear"},
    {202, kButtonY, 80, kButtonH, "Learn"},
};

// Volume slider: caption, then the track, then the value readout. The hit
// rectangle is a little wider and taller than the track so the handle is easy
// to grab; a click anywhere in it jumps the value.
constexpr int kVolumeCaptionX = 300;
constexpr int kVolumeTrackX = 360, kVolumeTrackW = 312;
constexpr int kVolumeTrackH = 6;
constexpr int kVolumeKnobR = 7;
constexpr int kVolumeHitX = kVolumeTrackX - 8, kVolumeHitW = kVolumeTrackW + 16;
constexpr int kVolumeHitY = kButtonY - 6, kVolumeHitH = kButtonH + 12;
constexpr int kVolumeCenterY = kVolumeHitY + kVolumeHitH / 2;
constexpr int kVolumeValueX = 692; // left edge of the "0.80" readout

// Title wordmark, bottom-left of the strip.
constexpr int kTitleX = 16;
constexpr int kTitleBaseline = kStripY + kStripH - 12;

// "Expand kit" — opens the rack page. Bottom row of the strip, opposite the
// wordmark, which is the only free space there.
constexpr int kExpandX = 592, kExpandW = 152;
constexpr int kExpandY = kStripY + 74, kExpandH = 30;

// --- Kit rack page -------------------------------------------------------
// A full-window page drawn over the kit: one row per shown slot, so the slots
// the nine pads do not cover are still reachable. The page owns the whole
// canvas while it is open, so its coordinates are absolute like everything
// else here, not relative to a sub-rectangle.

constexpr int kRackTitleX = 24;
constexpr int kRackTitleBaseline = 38;
constexpr int kRackCountX = 470;    // "N of 64 slots shown", right of the title
constexpr int kRackHeaderRule = 56; // horizontal rule under the header

// Row band. The rows are a fixed-height window into slots [0, slot count);
// the wheel scrolls it. kRackRowsY + kRackVisibleRows * kRackRowH must stay
// above kRackFooterRule.
constexpr int kRackRowsY = 64;
constexpr int kRackRowH = 32;
constexpr int kRackVisibleRows = 11;
constexpr int kRackRowLeft = 16, kRackRowRight = 736;
constexpr int kRackScrollX = 742, kRackScrollW = 8;

// Row columns (absolute x; y comes from the row).
constexpr int kRackIndexX = 22;
constexpr int kRackNameX = 50, kRackNameW = 78;
constexpr int kRackFileX = 132, kRackFileW = 180;
constexpr int kRackVolX = 318, kRackVolW = 100;
constexpr int kRackVolTrackH = 4, kRackVolKnobR = 5;
constexpr int kRackVolValueX = 424;
constexpr int kRackNoteX = 464, kRackNoteW = 70;

// Per-row buttons, in the row's own vertical band.
constexpr int kRackButtonInset = 5; // from the row top
constexpr int kRackButtonH = 22;
struct RackButtonSpec {
    int x, w;
    const char *label;
};
constexpr int kRackButtonCount = 3;
enum RackButtonIndex { kRackLoadButton = 0, kRackClearButton = 1, kRackLearnButton = 2 };
constexpr RackButtonSpec kRackButtons[kRackButtonCount] = {
    {540, 62, "Load\u2026"},
    {606, 58, "Clear"},
    {668, 64, "Learn"},
};

// Footer: the controls that grow and shrink the shown row count.
constexpr int kRackFooterRule = 424;
constexpr int kRackFooterY = 436, kRackFooterH = 28;
constexpr int kRackAddX = 22, kRackAddW = 116;
constexpr int kRackRemoveX = 146, kRackRemoveW = 132;
constexpr int kRackHintX = 296; // dim usage hint, right of the footer buttons

// Close box, top right. Drawn as an X, hit-tested with a margin.
constexpr int kRackCloseL = 716, kRackCloseT = 16, kRackCloseR = 740, kRackCloseB = 40;

} // namespace geo
} // namespace DRUMix
