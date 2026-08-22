// DRUMix native editor implementation.
//
// The panel is painted by drawPanel() (see drumpanel.h); this file keeps the
// state that drives it and turns pointer input into parameter edits: clicking
// a pad selects its slot, the strip's Load/Clear/Learn act on that slot, and
// the volume slider edits (kSlotVolumeBase + slot).
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

Vst::ParamID DrumEditorView::volumeParam() const
{
    return static_cast<Vst::ParamID>(kSlotVolumeBase + selectedSlot());
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
    return -1;
}

// The controller is the single source of truth for everything the strip shows,
// so the panel state is refilled from it rather than tracked in parallel.
void DrumEditorView::refreshPanel()
{
    const int slot = selectedSlot();

    std::string path;
    mPanel.sampleLoaded = sampleFile(slot, path);
    mPanel.sampleName = mPanel.sampleLoaded ? baseName(path) : "";

    const double noteNorm = paramValue(static_cast<Vst::ParamID>(kSlotNoteBase + slot));
    const int note = static_cast<int>(noteNorm * static_cast<double>(kNoteUnassigned) + 0.5);
    mPanel.note = (note >= kNoteUnassigned) ? -1 : note;

    mPanel.volume = paramValue(volumeParam());
}

//------------------------------------------------------------------------
void DrumEditorView::onDraw(cairo_t *cr)
{
    Canvas c(cr, &mFonts, static_cast<float>(geo::kWinW), static_cast<float>(geo::kWinH));
    refreshPanel();
    drawPanel(c, mImages, mPanel);
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

    // The browser is modal: while it is up it gets every click, including the
    // ones that dismiss it.
    if (mBrowser.isOpen()) {
        if (mBrowser.handleClick(fx, fy) == FileBrowser::Result::Chosen && mController) {
            const std::string &chosen = mBrowser.chosenPath();
            if (mController->setSampleFile(mBrowserSlot, chosen.c_str()) != kResultOk)
                fprintf(stderr, "DRUMix: loading '%s' failed\n", chosen.c_str());
        }
        invalidate();
        return;
    }

    const int btn = buttonAt(fx, fy);
    if (btn >= 0) {
        mPanel.pressedButton = btn;
        invalidate();
        return;
    }

    if (volumeHitRect().contains(fx, fy)) {
        mVolumeDrag = true;
        if (mController)
            mController->beginEdit(volumeParam());
        editVolume(volumeForX(fx));
        return;
    }

    const int pad = padAt(fx, fy);
    if (pad >= 0)
        selectPad(pad);
}

void DrumEditorView::onMouseMove(int x, int y)
{
    if (mVolumeDrag)
        editVolume(volumeForX(static_cast<float>(x)));
}

void DrumEditorView::onMouseUp(int x, int y, int button)
{
    if (button != 1)
        return;

    if (mVolumeDrag) {
        mVolumeDrag = false;
        if (mController)
            mController->endEdit(volumeParam());
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
            openBrowser();
            break;
        case geo::kClearButton:
            clearSample();
            break;
        case geo::kLearnButton:
            toggleLearn();
            break;
        default:
            break;
    }
}

void DrumEditorView::onMouseWheel(int x, int y, int delta)
{
    if (mBrowser.isOpen()) {
        if (mBrowser.handleWheel(delta))
            invalidate();
        return;
    }
    if (!volumeHitRect().contains(static_cast<float>(x), static_cast<float>(y)))
        return;

    double norm = paramValue(volumeParam()) + delta * kWheelStep;
    norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
    if (mController)
        mController->beginEdit(volumeParam());
    editVolume(norm);
    if (mController)
        mController->endEdit(volumeParam());
}

void DrumEditorView::onTick()
{
    bool animating = (mPanel.armedSlot >= 0);
    for (int i = 0; i < geo::kPadCount; ++i) {
        if (mPanel.hitLevel[i] > 0.0f) {
            mPanel.hitLevel[i] *= kHitDecay;
            if (mPanel.hitLevel[i] < kHitFloor)
                mPanel.hitLevel[i] = 0.0f;
            animating = true;
        }
    }
    ++mPanel.pulsePhase;
    if (animating)
        invalidate();
}

//------------------------------------------------------------------------
void DrumEditorView::selectPad(int pad)
{
    if (pad == mPanel.selectedPad)
        return;
    mPanel.selectedPad = pad;
    invalidate();
}

void DrumEditorView::openBrowser()
{
    mBrowserSlot = selectedSlot();
    std::string current;
    sampleFile(mBrowserSlot, current);
    char title[64];
    std::snprintf(title, sizeof(title), "Load sample — %s", geo::kPads[mPanel.selectedPad].name);
    mBrowser.open(current, "wav", title);
    invalidate();
}

void DrumEditorView::clearSample()
{
    if (mController && mController->setSampleFile(selectedSlot(), "") != kResultOk)
        fprintf(stderr, "DRUMix: clearing the sample failed\n");
    invalidate();
}

void DrumEditorView::toggleLearn()
{
    if (!mController)
        return;
    const int slot = selectedSlot();
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
void DrumEditorView::editVolume(double norm)
{
    if (!mController)
        return;
    const Vst::ParamID id = volumeParam();
    mController->setParamNormalized(id, norm);
    mController->performEdit(id, norm);
    invalidate();
}

//------------------------------------------------------------------------
void DrumEditorView::ParamChanged(Vst::ParamID id, Vst::ParamValue value)
{
    // Pad activity: the processor pulses the trigger velocity here, which is
    // what makes a pad flash when its note arrives.
    if (id >= static_cast<Vst::ParamID>(kSlotActivityBase) &&
        id < static_cast<Vst::ParamID>(kSlotActivityBase + kMaxSlots)) {
        const int pad = padForSlot(static_cast<int>(id - kSlotActivityBase));
        if (pad >= 0) {
            const float level = 0.45f + 0.55f * static_cast<float>(value);
            mPanel.hitLevel[pad] = level > 1.0f ? 1.0f : level;
            invalidate();
        }
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

    if (id >= static_cast<Vst::ParamID>(kSlotVolumeBase) &&
        id < static_cast<Vst::ParamID>(kSlotVolumeBase + kMaxSlots))
        invalidate();
}

} // namespace DRUMix
