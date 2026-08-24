// DRUMix native editor implementation.
//
// Two pages share one window. The kit page is painted by drawPanel(): clicking
// a pad selects its slot, and the strip's Load/Clear/Learn and volume slider
// act on that slot. The rack page, painted by drawRack(), is the way to the
// slots no pad drives: one row per shown slot, each with the same four
// controls, plus the buttons that grow and shrink the shown row count
// (kSlotCountId). Both pages edit the same parameters; neither owns state the
// controller does not already hold.
//
// Threading: everything here runs on the host's run-loop thread (the
// kPlatformTypeX11EmbedWindowID contract); the controller is called on the same
// thread, so value reads/writes and ParamChanged() need no locking.

#include "drumview.h"
#include "drumcontroller.h"
#include "platform/respath.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Steinberg;

namespace DRUMix
{

namespace
{
constexpr float kHitDecay = 0.80f;  // per tick, as in the Haiku original
constexpr float kHitFloor = 0.06f;  // below this the flash is over
constexpr double kWheelStep = 0.02; // volume nudge per wheel step

const char *baseName(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1;
}

// Point-in-pad tests (dx/dy relative to the pad centre, r = circumradius).
bool insideHexFlatTop(float dx, float dy, float r)
{
    const float s3 = 1.7320508f;
    dx = std::fabs(dx);
    dy = std::fabs(dy);
    return dy <= s3 * 0.5f * r && s3 * dx + dy <= s3 * r;
}

bool insideCircle(float dx, float dy, float r)
{
    return dx * dx + dy * dy <= r * r;
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

double clampNorm(double v)
{
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}
} // namespace

//------------------------------------------------------------------------
// static_cast, not reinterpret_cast: DrumController inherits from both
// EditController and IDrumLoader, so converting to the base may need a pointer
// adjustment that only a static_cast performs.
DrumEditorView::DrumEditorView(DrumController *controller)
    : X11PlugView(static_cast<Vst::EditController *>(controller)), mController(controller)
{
    ViewRect size(0, 0, geo::kWinW, geo::kWinH);
    setRect(size);
    mRack.maxSlots = static_cast<int>(kMaxSlots);
}

//------------------------------------------------------------------------
// Art and fonts are loaded here rather than in the constructor: createView()
// must stay harmless in headless hosts, which create and destroy views without
// ever attaching them.
void DrumEditorView::onAttached()
{
    if (mResourcesLoaded)
        return;
    const std::string &res = resourceDir();
    mFonts.load(res);
    mImages.setResourceDir(res);
    mResourcesLoaded = true;
}

//------------------------------------------------------------------------
double DrumEditorView::paramValue(Vst::ParamID id) const
{
    return mController ? mController->getParamNormalized(id) : 0.0;
}

Vst::ParamID DrumEditorView::volumeParam(int slot) const
{
    return static_cast<Vst::ParamID>(kSlotVolumeBase + slot);
}

int DrumEditorView::slotNote(int slot) const
{
    const double norm = paramValue(static_cast<Vst::ParamID>(kSlotNoteBase + slot));
    const int note = static_cast<int>(norm * static_cast<double>(kNoteUnassigned) + 0.5);
    return note >= kNoteUnassigned ? -1 : note;
}

bool DrumEditorView::sampleFile(int slot, std::string &out) const
{
    out.clear();
    if (!mController)
        return false;
    char buf[4096] = "";
    if (mController->getSampleFile(slot, buf, sizeof(buf)) != kResultOk || buf[0] == 0)
        return false;
    out = buf;
    return true;
}

// The "Slots" parameter is a 1..kMaxSlots range, so its plain value is the
// number of rows the rack shows.
int DrumEditorView::slotCount() const
{
    const double norm = paramValue(static_cast<Vst::ParamID>(kSlotCountId));
    const int plain = 1 + static_cast<int>(norm * static_cast<double>(kMaxSlots - 1) + 0.5);
    return clampInt(plain, 1, static_cast<int>(kMaxSlots));
}

int DrumEditorView::selectedSlot() const
{
    return geo::kPads[mPanel.selectedPad].slot;
}

int DrumEditorView::padForSlot(int slot) const
{
    for (int i = 0; i < geo::kPadCount; ++i) {
        if (geo::kPads[i].slot == slot)
            return i;
    }
    return -1;
}

int DrumEditorView::padAt(float x, float y) const
{
    for (int i = 0; i < geo::kPadCount; ++i) {
        const geo::PadSpec &p = geo::kPads[i];
        const float dx = x - static_cast<float>(p.x);
        const float dy = y - static_cast<float>(p.y);
        const float r = static_cast<float>(p.r);
        if (p.hex ? insideHexFlatTop(dx, dy, r) : insideCircle(dx, dy, r))
            return i;
    }
    return -1;
}

int DrumEditorView::buttonAt(float x, float y) const
{
    for (int i = 0; i < geo::kButtonCount; ++i) {
        if (buttonRect(i).contains(x, y))
            return i;
    }
    // The Expand kit button sits just past the strip buttons; see PanelState.
    if (expandButtonRect().contains(x, y))
        return geo::kButtonCount;
    return -1;
}

// The controller is the single source of truth for everything the pages show,
// so their state is refilled from it rather than tracked in parallel.
void DrumEditorView::refreshPanel()
{
    const int slot = selectedSlot();

    std::string path;
    mPanel.sampleLoaded = sampleFile(slot, path);
    mPanel.sampleName = mPanel.sampleLoaded ? baseName(path) : "";
    mPanel.note = slotNote(slot);
    mPanel.volume = paramValue(volumeParam(slot));

    for (int i = 0; i < geo::kPadCount; ++i)
        mPanel.hitLevel[i] = mHit[geo::kPads[i].slot];
}

void DrumEditorView::refreshRack()
{
    const int count = slotCount();
    const int maxFirst = count > geo::kRackVisibleRows ? count - geo::kRackVisibleRows : 0;

    mRack.slotCount = count;
    mRack.maxSlots = static_cast<int>(kMaxSlots);
    mRack.firstRow = clampInt(mRack.firstRow, 0, maxFirst);
    mRack.selectedSlot = selectedSlot();
    mRack.pulsePhase = mPanel.pulsePhase;

    const int shown = count - mRack.firstRow < geo::kRackVisibleRows ? count - mRack.firstRow
                                                                     : geo::kRackVisibleRows;
    mRack.rows.resize(static_cast<size_t>(shown < 0 ? 0 : shown));

    std::string path;
    for (size_t i = 0; i < mRack.rows.size(); ++i) {
        RackRow &row = mRack.rows[i];
        const int slot = mRack.firstRow + static_cast<int>(i);
        const int pad = padForSlot(slot);

        row.slot = slot;
        row.name = pad >= 0 ? geo::kPads[pad].name : nullptr;
        row.sampleLoaded = sampleFile(slot, path);
        row.sampleName = row.sampleLoaded ? baseName(path) : "";
        row.note = slotNote(slot);
        row.volume = paramValue(volumeParam(slot));
        row.armed = (slot == mPanel.armedSlot);
        row.hitLevel = mHit[slot];
    }
}

//------------------------------------------------------------------------
void DrumEditorView::onDraw(cairo_t *cr)
{
    Canvas c(cr, &mFonts, static_cast<float>(geo::kWinW), static_cast<float>(geo::kWinH));
    if (mRackOpen) {
        refreshRack();
        drawRack(c, mRack);
    } else {
        refreshPanel();
        drawPanel(c, mImages, mPanel);
    }
    mBrowser.draw(c);
}

//------------------------------------------------------------------------
// Interaction
//------------------------------------------------------------------------
void DrumEditorView::onMouseDown(int x, int y, int button)
{
    if (button != 1)
        return;
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);

    // The browser is modal over either page: while it is up it gets every
    // click, including the ones that dismiss it.
    if (mBrowser.isOpen()) {
        if (mBrowser.handleClick(fx, fy) == FileBrowser::Result::Chosen && mController) {
            const std::string &chosen = mBrowser.chosenPath();
            if (mController->setSampleFile(mBrowserSlot, chosen.c_str()) != kResultOk)
                fprintf(stderr, "DRUMix: loading '%s' failed\n", chosen.c_str());
        }
        invalidate();
        return;
    }

    if (mRackOpen) {
        onRackMouseDown(fx, fy);
        return;
    }

    const int btn = buttonAt(fx, fy);
    if (btn >= 0) {
        mPanel.pressedButton = btn;
        invalidate();
        return;
    }

    if (volumeHitRect().contains(fx, fy)) {
        startVolumeDrag(selectedSlot(), volumeForX(fx));
        return;
    }

    const int pad = padAt(fx, fy);
    if (pad >= 0)
        selectPad(pad);
}

void DrumEditorView::onMouseMove(int x, int y)
{
    if (mDragSlot < 0)
        return;
    const float fx = static_cast<float>(x);
    editVolume(mDragSlot, mRackOpen ? rackVolumeForX(fx) : volumeForX(fx));
}

void DrumEditorView::onMouseUp(int x, int y, int button)
{
    if (button != 1)
        return;

    if (mDragSlot >= 0) {
        endVolumeDrag();
        return;
    }

    if (mRackOpen) {
        onRackMouseUp(static_cast<float>(x), static_cast<float>(y));
        return;
    }

    if (mPanel.pressedButton < 0)
        return;
    const int released = mPanel.pressedButton;
    mPanel.pressedButton = -1;
    invalidate();
    // Only act when the release lands on the button that was pressed, so a
    // press dragged off it cancels, as it does everywhere else.
    if (buttonAt(static_cast<float>(x), static_cast<float>(y)) != released)
        return;

    switch (released) {
        case geo::kLoadButton:
            openBrowser(selectedSlot(), geo::kPads[mPanel.selectedPad].name);
            break;
        case geo::kClearButton:
            clearSample(selectedSlot());
            break;
        case geo::kLearnButton:
            toggleLearn(selectedSlot());
            break;
        case geo::kButtonCount:
            openRack();
            break;
        default:
            break;
    }
}

void DrumEditorView::onMouseWheel(int x, int y, int delta)
{
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);

    if (mBrowser.isOpen()) {
        if (mBrowser.handleWheel(delta))
            invalidate();
        return;
    }
    if (mRackOpen) {
        onRackMouseWheel(fx, fy, delta);
        return;
    }
    if (!volumeHitRect().contains(fx, fy))
        return;

    const int slot = selectedSlot();
    const double norm = clampNorm(paramValue(volumeParam(slot)) + delta * kWheelStep);
    if (mController)
        mController->beginEdit(volumeParam(slot));
    editVolume(slot, norm);
    if (mController)
        mController->endEdit(volumeParam(slot));
}

void DrumEditorView::onTick()
{
    bool animating = (mPanel.armedSlot >= 0);
    for (int i = 0; i < kMaxSlots; ++i) {
        if (mHit[i] > 0.0f) {
            mHit[i] *= kHitDecay;
            if (mHit[i] < kHitFloor)
                mHit[i] = 0.0f;
            animating = true;
        }
    }
    ++mPanel.pulsePhase;
    if (animating)
        invalidate();
}

//------------------------------------------------------------------------
// Actions, all of them addressed by slot so the two pages share them
//------------------------------------------------------------------------
void DrumEditorView::selectPad(int pad)
{
    if (pad == mPanel.selectedPad)
        return;
    mPanel.selectedPad = pad;
    invalidate();
}

void DrumEditorView::openBrowser(int slot, const char *what)
{
    mBrowserSlot = slot;
    std::string current;
    sampleFile(slot, current);
    char title[96];
    std::snprintf(title, sizeof(title), "Load sample — %s", what);
    mBrowser.open(current, "wav", title);
    invalidate();
}

void DrumEditorView::clearSample(int slot)
{
    if (mController && mController->setSampleFile(slot, "") != kResultOk)
        fprintf(stderr, "DRUMix: clearing the sample failed\n");
    invalidate();
}

void DrumEditorView::toggleLearn(int slot)
{
    if (!mController)
        return;
    if (mPanel.armedSlot == slot) {
        mController->armLearn(-1);
        mPanel.armedSlot = -1;
    } else if (mController->armLearn(slot) == kResultOk) {
        mPanel.armedSlot = slot;
    } else {
        fprintf(stderr, "DRUMix: arming MIDI learn failed\n");
    }
    invalidate();
}

// The host is told about the edit and the drawn value is read back from the
// controller, so there is exactly one source of truth for it.
void DrumEditorView::editVolume(int slot, double norm)
{
    if (!mController)
        return;
    const Vst::ParamID id = volumeParam(slot);
    mController->setParamNormalized(id, norm);
    mController->performEdit(id, norm);
    invalidate();
}

void DrumEditorView::startVolumeDrag(int slot, double norm)
{
    mDragSlot = slot;
    if (mController)
        mController->beginEdit(volumeParam(slot));
    editVolume(slot, norm);
}

void DrumEditorView::endVolumeDrag()
{
    if (mDragSlot < 0)
        return;
    if (mController)
        mController->endEdit(volumeParam(mDragSlot));
    mDragSlot = -1;
}

//------------------------------------------------------------------------
// The rack page
//------------------------------------------------------------------------
void DrumEditorView::openRack()
{
    mRackOpen = true;
    // Open on the selected slot, so the page starts where the kit page left
    // off rather than always at the top.
    const int count = slotCount();
    const int maxFirst = count > geo::kRackVisibleRows ? count - geo::kRackVisibleRows : 0;
    mRack.firstRow = clampInt(selectedSlot() - geo::kRackVisibleRows / 2, 0, maxFirst);
    invalidate();
}

void DrumEditorView::closeRack()
{
    mRackOpen = false;
    mRack.pressedRow = -1;
    mRack.pressedButton = -1;
    mRack.pressedControl = kRackNothing;
    invalidate();
}

void DrumEditorView::scrollRack(int rows)
{
    const int count = slotCount();
    const int maxFirst = count > geo::kRackVisibleRows ? count - geo::kRackVisibleRows : 0;
    const int next = clampInt(mRack.firstRow + rows, 0, maxFirst);
    if (next == mRack.firstRow)
        return;
    mRack.firstRow = next;
    invalidate();
}

// Growing the rack only reveals rows: every slot already has its parameters,
// and the DSP plays any slot with a sample and a bound note whether or not its
// row is shown.
void DrumEditorView::setSlotCount(int count)
{
    if (!mController)
        return;
    const int current = slotCount();
    const int wanted = clampInt(count, 1, static_cast<int>(kMaxSlots));
    if (wanted == current)
        return;

    const Vst::ParamID id = static_cast<Vst::ParamID>(kSlotCountId);
    const double norm = static_cast<double>(wanted - 1) / static_cast<double>(kMaxSlots - 1);
    mController->beginEdit(id);
    mController->setParamNormalized(id, norm);
    mController->performEdit(id, norm);
    mController->endEdit(id);

    // Scroll the new row into view when the list grows; when it shrinks, just
    // keep the window inside the shorter list.
    const int maxFirst = wanted > geo::kRackVisibleRows ? wanted - geo::kRackVisibleRows : 0;
    mRack.firstRow = (wanted > current) ? maxFirst : clampInt(mRack.firstRow, 0, maxFirst);
    invalidate();
}

int DrumEditorView::rackRowAt(float x, float y) const
{
    for (int i = 0; i < geo::kRackVisibleRows; ++i) {
        if (rackRowRect(i).contains(x, y))
            return i;
    }
    return -1;
}

int DrumEditorView::rackSlotAt(float x, float y) const
{
    const int position = rackRowAt(x, y);
    if (position < 0)
        return -1;
    const int slot = mRack.firstRow + position;
    return slot < slotCount() ? slot : -1;
}

int DrumEditorView::rackButtonAt(int position, float x, float y) const
{
    if (position < 0)
        return -1;
    for (int i = 0; i < geo::kRackButtonCount; ++i) {
        if (rackButtonRect(position, i).contains(x, y))
            return i;
    }
    return -1;
}

void DrumEditorView::onRackMouseDown(float x, float y)
{
    if (rackCloseBox().inset(-8.0f).contains(x, y)) {
        mRack.pressedControl = kRackClose;
        invalidate();
        return;
    }
    if (rackAddRect().contains(x, y)) {
        mRack.pressedControl = kRackAdd;
        invalidate();
        return;
    }
    if (rackRemoveRect().contains(x, y)) {
        mRack.pressedControl = kRackRemove;
        invalidate();
        return;
    }

    const int position = rackRowAt(x, y);
    const int slot = rackSlotAt(x, y);
    if (slot < 0)
        return;

    const int button = rackButtonAt(position, x, y);
    if (button >= 0) {
        mRack.pressedRow = slot;
        mRack.pressedButton = button;
        invalidate();
        return;
    }

    if (rackVolumeHitRect(position).contains(x, y)) {
        startVolumeDrag(slot, rackVolumeForX(x));
        return;
    }

    // Clicking the row of a slot a pad drives selects that pad, so closing the
    // page lands on the slot that was last touched here.
    const int pad = padForSlot(slot);
    if (pad >= 0)
        selectPad(pad);
}

void DrumEditorView::onRackMouseUp(float x, float y)
{
    // Page controls: act only when the release lands on the control that was
    // pressed, as on the kit page.
    if (mRack.pressedControl != kRackNothing) {
        const RackControl released = mRack.pressedControl;
        mRack.pressedControl = kRackNothing;
        invalidate();
        switch (released) {
            case kRackClose:
                if (rackCloseBox().inset(-8.0f).contains(x, y))
                    closeRack();
                break;
            case kRackAdd:
                if (rackAddRect().contains(x, y))
                    setSlotCount(slotCount() + 1);
                break;
            case kRackRemove:
                if (rackRemoveRect().contains(x, y))
                    setSlotCount(slotCount() - 1);
                break;
            default:
                break;
        }
        return;
    }

    if (mRack.pressedRow < 0)
        return;
    const int slot = mRack.pressedRow;
    const int button = mRack.pressedButton;
    mRack.pressedRow = -1;
    mRack.pressedButton = -1;
    invalidate();

    const int position = slot - mRack.firstRow;
    if (rackButtonAt(position, x, y) != button)
        return;

    char what[32];
    const int pad = padForSlot(slot);
    if (pad >= 0)
        std::snprintf(what, sizeof(what), "%s", geo::kPads[pad].name);
    else
        std::snprintf(what, sizeof(what), "slot %d", slot + 1);

    switch (button) {
        case geo::kRackLoadButton:
            openBrowser(slot, what);
            break;
        case geo::kRackClearButton:
            clearSample(slot);
            break;
        case geo::kRackLearnButton:
            toggleLearn(slot);
            break;
        default:
            break;
    }
}

bool DrumEditorView::onRackMouseWheel(float x, float y, int delta)
{
    // Over a row's volume the wheel edits it; anywhere else it scrolls, which
    // is the only way to reach slot 64.
    const int position = rackRowAt(x, y);
    const int slot = rackSlotAt(x, y);
    if (slot >= 0 && rackVolumeHitRect(position).contains(x, y)) {
        const Vst::ParamID id = volumeParam(slot);
        const double norm = clampNorm(paramValue(id) + delta * kWheelStep);
        if (mController)
            mController->beginEdit(id);
        editVolume(slot, norm);
        if (mController)
            mController->endEdit(id);
        return true;
    }

    scrollRack(-delta);
    return true;
}

//------------------------------------------------------------------------
void DrumEditorView::ParamChanged(Vst::ParamID id, Vst::ParamValue value)
{
    // Pad activity: the processor pulses the trigger velocity here, which is
    // what makes a pad (and its rack row) flash when its note arrives.
    if (id >= static_cast<Vst::ParamID>(kSlotActivityBase) &&
        id < static_cast<Vst::ParamID>(kSlotActivityBase + kMaxSlots)) {
        const int slot = static_cast<int>(id - kSlotActivityBase);
        const float level = 0.45f + 0.55f * static_cast<float>(value);
        mHit[slot] = level > 1.0f ? 1.0f : level;
        invalidate();
        return;
    }

    // A note arriving for the armed slot is the MIDI learn completing.
    if (id >= static_cast<Vst::ParamID>(kSlotNoteBase) &&
        id < static_cast<Vst::ParamID>(kSlotNoteBase + kMaxSlots)) {
        if (static_cast<int>(id - kSlotNoteBase) == mPanel.armedSlot)
            mPanel.armedSlot = -1;
        invalidate();
        return;
    }

    if (id == static_cast<Vst::ParamID>(kSlotCountId)) {
        invalidate();
        return;
    }

    if (id >= static_cast<Vst::ParamID>(kSlotVolumeBase) &&
        id < static_cast<Vst::ParamID>(kSlotVolumeBase + kMaxSlots))
        invalidate();
}

} // namespace DRUMix
