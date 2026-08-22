// drumix-standalone — run the DRUMix VST3 without a DAW.
//
// This is a deliberately small host: one top-level X window, the plug-in's own
// editor embedded inside it, a run loop the plug-in can register with, and a
// JACK client feeding the processor MIDI and taking its audio. It exists so
// DRUMix is usable on its own — plug in a kit, load samples, play — not as a
// general-purpose host.
//
// The plug-in is loaded as a bundle through the SDK's module loader rather
// than linked in. That matters: the editor locates its art and fonts with
// dladdr() relative to its own .so, so it must genuinely be a loaded module
// for the resource paths to resolve the same way they do inside a DAW.
//
// Threading: everything except the JACK callback runs on this thread. The
// editor, the controller and the run loop are all single-threaded here, which
// is the same contract a DAW provides.

#include "jackclient.h"
#include "runloop.h"

#include "drumids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace Steinberg;

namespace
{

// Mirrors the editor's fixed canvas (drumgeometry.h). The standalone window
// is not resizable for the same reason the editor is not.
constexpr int kWinW = 760;
constexpr int kWinH = 480;

DRUMix::RunLoop *gRunLoop = nullptr;

void onSignal(int)
{
    if (gRunLoop)
        gRunLoop->stop();
}

//------------------------------------------------------------------------
// The host end of the VST3 edit loop. The editor calls performEdit() when the
// user moves a control; we forward the value to the processor through the
// JACK client's lock-free ring.
class ComponentHandler : public Vst::IComponentHandler
{
public:
    ComponentHandler(DRUMix::JackClient &jack, Vst::IEditController *controller)
        : mJack(jack), mController(controller)
    {
    }

    tresult PLUGIN_API beginEdit(Vst::ParamID) SMTG_OVERRIDE
    {
        return kResultOk;
    }

    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) SMTG_OVERRIDE
    {
        mJack.pushParameter(id, value);
        return kResultOk;
    }

    tresult PLUGIN_API endEdit(Vst::ParamID) SMTG_OVERRIDE
    {
        return kResultOk;
    }

    tresult PLUGIN_API restartComponent(int32) SMTG_OVERRIDE
    {
        return kResultOk;
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) SMTG_OVERRIDE
    {
        if (!obj)
            return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<Vst::IComponentHandler *>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE
    {
        return 1000;
    }
    uint32 PLUGIN_API release() SMTG_OVERRIDE
    {
        return 1000;
    }

private:
    DRUMix::JackClient &mJack;
    Vst::IEditController *mController;
};

//------------------------------------------------------------------------
// Re-runs setupProcessing when JACK changes its buffer size under the running
// client. The chunk loop in JackClient keeps audio correct without this — no
// block ever reaches the processor larger than the size it was set up for —
// but the processor would otherwise stay configured for the size it saw at
// startup, sizing its internal buffers and its reported latency for a block
// the host is no longer sending.
//
// This runs on the run loop, not in JACK's buffer-size callback: setActive and
// setupProcessing are VST3 main-thread calls, and the plug-in's message thread
// may be part-way through a load. JackClient::suspendProcessing() is what
// keeps the audio callback out of the processor while it is reconfigured.
class BufferSizeWatcher : public Linux::ITimerHandler
{
public:
    BufferSizeWatcher(DRUMix::JackClient &jack, Vst::IComponent *component,
                      Vst::IAudioProcessor *processor, const Vst::ProcessSetup &setup)
        : mJack(jack), mComponent(component), mProcessor(processor), mSetup(setup)
    {
    }

    void PLUGIN_API onTimer() SMTG_OVERRIDE
    {
        const int size = mJack.takeBufferSizeChange();
        if (size <= 0)
            return;

        if (!mJack.suspendProcessing()) {
            fprintf(stderr,
                    "drumix-standalone: the audio thread did not respond, so the processor "
                    "was left set up for %d frames\n",
                    mJack.blockSize());
            return;
        }

        mProcessor->setProcessing(false);
        mComponent->setActive(false);

        Vst::ProcessSetup setup = mSetup;
        setup.maxSamplesPerBlock = size;
        const bool ok = mProcessor->setupProcessing(setup) == kResultOk;
        if (ok)
            mSetup = setup;
        else
            fprintf(stderr, "drumix-standalone: the plug-in refused %d frames; keeping %d\n", size,
                    mSetup.maxSamplesPerBlock);

        mComponent->setActive(true);
        mProcessor->setProcessing(true);
        // Only adopt the new chunk size if the plug-in accepted it. If it did
        // not, the old size is still what it is prepared for, and the chunk
        // loop must keep honouring that.
        mJack.resumeProcessing(ok ? size : mSetup.maxSamplesPerBlock);

        printf("drumix-standalone: JACK buffer size is now %d frames\n", mJack.blockSize());
        fflush(stdout);
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) SMTG_OVERRIDE
    {
        if (!obj)
            return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(iid, Linux::ITimerHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<Linux::ITimerHandler *>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE
    {
        return 1000;
    }
    uint32 PLUGIN_API release() SMTG_OVERRIDE
    {
        return 1000;
    }

private:
    DRUMix::JackClient &mJack;
    Vst::IComponent *mComponent = nullptr;
    Vst::IAudioProcessor *mProcessor = nullptr;
    Vst::ProcessSetup mSetup;
};

//------------------------------------------------------------------------
// Feeds the parameter changes the plug-in published from the audio thread back
// into the controller: the pad activity pulses that flash the kit, and the note
// a MIDI learn captured. In a DAW this is the host's job; here it is a run-loop
// timer, so no IEditController call ever happens on the RT thread.
class OutputParamPump : public Linux::ITimerHandler
{
public:
    OutputParamPump(DRUMix::JackClient &jack, Vst::IEditController *controller)
        : mJack(jack), mController(controller)
    {
    }

    void PLUGIN_API onTimer() SMTG_OVERRIDE
    {
        if (!mController)
            return;
        Vst::ParamID id = 0;
        Vst::ParamValue value = 0.0;
        // Bounded: a wedged audio thread must not be able to spin the UI.
        for (int i = 0; i < 128 && mJack.popOutputParameter(id, value); ++i)
            mController->setParamNormalized(id, value);
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) SMTG_OVERRIDE
    {
        if (!obj)
            return kInvalidArgument;
        if (FUnknownPrivate::iidEqual(iid, Linux::ITimerHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<Linux::ITimerHandler *>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE
    {
        return 1000;
    }
    uint32 PLUGIN_API release() SMTG_OVERRIDE
    {
        return 1000;
    }

private:
    DRUMix::JackClient &mJack;
    Vst::IEditController *mController;
};

} // namespace

//------------------------------------------------------------------------
// Where to look for DRUMix.vst3 when no path is given on the command line.
// The standalone is a host: it loads the same bundle a DAW would, rather than
// linking the plug-in in, so that dladdr-based resource lookup behaves
// identically in both. That means it has to be able to FIND the bundle --
// covering the release tarball (bundle beside the binary), the build tree, and
// a system or per-user VST3 install.
static std::string findBundle()
{
    std::vector<std::string> candidates;

    if (const char *env = getenv("DRUMIX_VST3"))
        candidates.emplace_back(env);

    char exe[4096] = {0};
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        std::string dir(exe, static_cast<size_t>(n));
        const size_t slash = dir.find_last_of('/');
        if (slash != std::string::npos)
            dir.resize(slash);
        candidates.push_back(dir + "/DRUMix.vst3");              // release tarball
        candidates.push_back(dir + "/VST3/Release/DRUMix.vst3"); // build tree
    }

    if (const char *home = getenv("HOME"))
        candidates.push_back(std::string(home) + "/.vst3/DRUMix.vst3");
    candidates.push_back("/usr/local/lib/vst3/DRUMix.vst3");
    candidates.push_back("/usr/lib/vst3/DRUMix.vst3");

    std::error_code ec;
    for (const auto &path : candidates)
        if (std::filesystem::is_directory(path, ec))
            return path;

    return candidates.empty() ? std::string() : candidates.front();
}

//------------------------------------------------------------------------
int main(int argc, char **argv)
{
    const std::string modulePath = argc > 1 ? std::string(argv[1]) : findBundle();

    // The host context must be published BEFORE the plug-in is instantiated:
    // ComponentBase::allocateMessage() asks it for IMessage instances, so
    // without one every controller->processor message (model path, IR path,
    // Slim) is silently dropped and only parameter changes get through. That
    // presents as a plug-in whose knobs work but which never loads a model.
    Vst::HostApplication hostContext;
    Vst::PluginContextFactory::instance().setPluginContext(&hostContext);

    std::string error;
    auto module = VST3::Hosting::Module::create(modulePath, error);
    if (!module) {
        fprintf(stderr, "drumix-standalone: cannot load %s\n  %s\n", modulePath.c_str(),
                error.c_str());
        fprintf(stderr,
                "usage: %s [path to DRUMix.vst3]\n"
                "  Searched, in order: $DRUMIX_VST3, alongside this binary,\n"
                "  the build tree, ~/.vst3, /usr/local/lib/vst3, /usr/lib/vst3.\n",
                argv[0]);
        return 1;
    }

    // --- instantiate the plug-in -------------------------------------
    auto factory = module->getFactory();
    IPtr<Vst::PlugProvider> provider;
    for (auto &classInfo : factory.classInfos()) {
        if (classInfo.category() != kVstAudioEffectClass)
            continue;
        provider = owned(new Vst::PlugProvider(factory, classInfo, true));
        if (provider->initialize())
            break;
        provider = nullptr;
    }
    if (!provider) {
        fprintf(stderr, "drumix-standalone: no audio effect class in %s\n", modulePath.c_str());
        return 1;
    }

    Vst::IComponent *component = provider->getComponent();
    Vst::IEditController *controller = provider->getController();
    if (!component || !controller) {
        fprintf(stderr, "drumix-standalone: the plug-in did not provide both parts\n");
        return 1;
    }

    FUnknownPtr<Vst::IAudioProcessor> processor(component);
    if (!processor) {
        fprintf(stderr, "drumix-standalone: the plug-in has no IAudioProcessor\n");
        return 1;
    }

    // --- audio -------------------------------------------------------
    DRUMix::JackClient jack;

    // A first connection just to learn the server's rate and block size, so
    // setupProcessing can be told the truth before the component is activated.
    jack_status_t status = static_cast<jack_status_t>(0);
    jack_client_t *probe = jack_client_open("DRUMix-probe", JackNoStartServer, &status);
    double sampleRate = 48000.0;
    int blockSize = 1024;
    if (probe) {
        sampleRate = static_cast<double>(jack_get_sample_rate(probe));
        blockSize = static_cast<int>(jack_get_buffer_size(probe));
        jack_client_close(probe);
    } else {
        fprintf(stderr, "drumix-standalone: no JACK server; "
                        "continuing with the editor only\n");
    }

    Vst::ProcessSetup setup = {};
    setup.processMode = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    if (processor->setupProcessing(setup) != kResultOk) {
        fprintf(stderr, "drumix-standalone: the plug-in rejected the process setup\n");
        return 1;
    }

    component->setActive(true);
    processor->setProcessing(true);

    ComponentHandler handler(jack, controller);
    controller->setComponentHandler(&handler);

    if (probe && !jack.open("DRUMix", processor, component))
        fprintf(stderr, "drumix-standalone: continuing without audio\n");

    // --- window and editor -------------------------------------------
    ::Display *display = XOpenDisplay(nullptr);
    if (!display) {
        fprintf(stderr, "drumix-standalone: cannot open the X display\n");
        return 1;
    }

    const int screen = DefaultScreen(display);
    ::Window window =
        XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, kWinW, kWinH, 0,
                            BlackPixel(display, screen), BlackPixel(display, screen));
    XStoreName(display, window, "DRUMix");
    XSelectInput(display, window, StructureNotifyMask | SubstructureNotifyMask);

    // Fixed size: the editor does not resize, so tell the window manager.
    XSizeHints hints = {};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = kWinW;
    hints.min_height = hints.max_height = kWinH;
    XSetWMNormalHints(display, window, &hints);

    Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDelete, 1);
    XMapWindow(display, window);
    XFlush(display);

    DRUMix::RunLoop runLoop(display);
    gRunLoop = &runLoop;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    IPtr<IPlugView> view = owned(controller->createView(Vst::ViewType::kEditor));
    if (view && view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) == kResultTrue) {
        view->setFrame(&runLoop);
        if (view->attached(reinterpret_cast<void *>(static_cast<uintptr_t>(window)),
                           kPlatformTypeX11EmbedWindowID) != kResultTrue) {
            fprintf(stderr, "drumix-standalone: the editor refused to attach\n");
            view = nullptr;
        }
    } else {
        fprintf(stderr, "drumix-standalone: the plug-in has no X11 editor\n");
        view = nullptr;
    }

    OutputParamPump outputParams(jack, controller);
    runLoop.registerTimer(&outputParams, 33);

    BufferSizeWatcher blockWatcher(jack, component, processor, setup);
    if (jack.isOpen())
        runLoop.registerTimer(&blockWatcher, 33);

    runLoop.setXEventCallback([&](const XEvent &event) {
        if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == wmDelete)
            runLoop.stop();
    });

    runLoop.run();

    // --- teardown ----------------------------------------------------
    if (jack.isOpen())
        runLoop.unregisterTimer(&blockWatcher);
    runLoop.unregisterTimer(&outputParams);
    if (view) {
        view->removed();
        view = nullptr;
    }
    jack.close();
    processor->setProcessing(false);
    component->setActive(false);
    controller->setComponentHandler(nullptr);

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    gRunLoop = nullptr;
    // Retract the host context before it leaves scope, so nothing can reach a
    // dangling pointer during static destruction.
    Vst::PluginContextFactory::instance().setPluginContext(nullptr);
    return 0;
}
