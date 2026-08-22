// JackClient implementation. See jackclient.h.

#include "jackclient.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <jack/midiport.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace Steinberg;

namespace DRUMix
{

namespace
{
// One JACK period's worth of note events is far more than a drummer can
// produce; the list is sized once and events beyond it are dropped rather
// than allocated for on the RT thread.
constexpr int32 kMaxEventsPerBlock = 512;
} // namespace

//------------------------------------------------------------------------
JackClient::~JackClient()
{
    close();
}

//------------------------------------------------------------------------
bool JackClient::open(const char *clientName, Vst::IAudioProcessor *processor,
                      Vst::IComponent *component)
{
    if (!processor || !component)
        return false;
    mProcessor = processor;
    mComponent = component;

    jack_status_t status = static_cast<jack_status_t>(0);
    mClient = jack_client_open(clientName, JackNoStartServer, &status);
    if (!mClient) {
        fprintf(stderr, "DRUMix: cannot connect to JACK (is jackd running?)\n");
        return false;
    }

    mSampleRate = static_cast<double>(jack_get_sample_rate(mClient));
    mBlockSize.store(static_cast<int>(jack_get_buffer_size(mClient)), std::memory_order_relaxed);

    mMidiPort = jack_port_register(mClient, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    mOutPorts[0] =
        jack_port_register(mClient, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    mOutPorts[1] =
        jack_port_register(mClient, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!mMidiPort || !mOutPorts[0] || !mOutPorts[1]) {
        fprintf(stderr, "DRUMix: cannot register JACK ports\n");
        close();
        return false;
    }

    // All RT-side allocation happens here, before the process callback can
    // run: the bus and channel-pointer arrays, parameter queues sized for
    // every parameter the plug-in exposes, the event list, and the silent
    // input block.
    //
    // bufferSamples is deliberately 0. That is what tells HostProcessData it
    // does NOT own the sample buffers, because process() below points the
    // buses straight at JACK's own memory each block. Passing mBlockSize here
    // instead makes it allocate a buffer per channel and set channelBufferOwner
    // — and then unprepare() runs delete[] on whatever the pointers hold at
    // close, which by then is JACK's memory, while the buffers it really
    // allocated leak.
    if (!mProcessData.prepare(*component, 0, Vst::kSample32)) {
        fprintf(stderr, "DRUMix: cannot prepare the process buffers\n");
        close();
        return false;
    }

    mEvents.setMaxSize(kMaxEventsPerBlock);
    mSilence.assign(static_cast<size_t>(mBlockSize.load(std::memory_order_relaxed)), 0.0f);

    mProcessData.numSamples = mBlockSize.load(std::memory_order_relaxed);
    mProcessData.symbolicSampleSize = Vst::kSample32;
    mProcessData.inputParameterChanges = &mInputChanges;
    mProcessData.outputParameterChanges = &mOutputChanges;
    mProcessData.inputEvents = &mEvents;

    if (jack_set_process_callback(mClient, processTrampoline, this) != 0) {
        fprintf(stderr, "DRUMix: cannot install the JACK process callback\n");
        close();
        return false;
    }
    // Must be registered before jack_activate() (jack.h says so explicitly).
    if (jack_set_buffer_size_callback(mClient, bufferSizeTrampoline, this) != 0)
        fprintf(stderr, "DRUMix: cannot install the JACK buffer-size callback - the processor "
                        "will stay set up for the size it started with\n");

    if (jack_activate(mClient) != 0) {
        fprintf(stderr, "DRUMix: cannot activate the JACK client\n");
        close();
        return false;
    }

    // Auto-connect: the outputs to the system's playback ports, and the MIDI
    // input to the first physical MIDI source, so a connected kit plays the
    // instrument out of the box. Failures here are not fatal — the user can
    // wire it up in a patchbay instead.
    if (const char **outs = jack_get_ports(mClient, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                           JackPortIsPhysical | JackPortIsInput)) {
        for (int i = 0; i < 2 && outs[i]; ++i)
            jack_connect(mClient, jack_port_name(mOutPorts[i]), outs[i]);
        jack_free(outs);
    }
    if (const char **midi = jack_get_ports(mClient, nullptr, JACK_DEFAULT_MIDI_TYPE,
                                           JackPortIsPhysical | JackPortIsOutput)) {
        if (midi[0])
            jack_connect(mClient, midi[0], jack_port_name(mMidiPort));
        jack_free(midi);
    }

    printf("DRUMix: JACK connected at %.0f Hz, %d frames\n", mSampleRate,
           mBlockSize.load(std::memory_order_relaxed));
    return true;
}

//------------------------------------------------------------------------
void JackClient::close()
{
    if (mClient) {
        jack_deactivate(mClient);
        jack_client_close(mClient);
        mClient = nullptr;
    }
    mMidiPort = nullptr;
    mOutPorts[0] = mOutPorts[1] = nullptr;
    mProcessData.unprepare();
    mProcessor = nullptr;
    mComponent = nullptr;
}

//------------------------------------------------------------------------
// UI thread (producer).
bool JackClient::pushParameter(Vst::ParamID id, Vst::ParamValue value)
{
    const uint32_t write = mRingWrite.load(std::memory_order_relaxed);
    const uint32_t next = (write + 1) % kRingSize;
    if (next == mRingRead.load(std::memory_order_acquire))
        return false; // full; drop rather than block either thread
    mRing[write] = {id, value};
    mRingWrite.store(next, std::memory_order_release);
    return true;
}

//------------------------------------------------------------------------
// UI thread (consumer of the RT -> UI ring).
bool JackClient::popOutputParameter(Vst::ParamID &id, Vst::ParamValue &value)
{
    const uint32_t read = mOutRingRead.load(std::memory_order_relaxed);
    if (read == mOutRingWrite.load(std::memory_order_acquire))
        return false;
    id = mOutRing[read].id;
    value = mOutRing[read].value;
    mOutRingRead.store((read + 1) % kRingSize, std::memory_order_release);
    return true;
}

//------------------------------------------------------------------------
// RT thread (consumer). addParameterData/addPoint only reuse the queues and
// points reserved during prepare(), so this does not allocate.
void JackClient::drainParameterRing()
{
    uint32_t read = mRingRead.load(std::memory_order_relaxed);
    const uint32_t write = mRingWrite.load(std::memory_order_acquire);
    while (read != write) {
        const Change &change = mRing[read];
        int32 index = 0;
        if (Vst::IParamValueQueue *queue = mInputChanges.addParameterData(change.id, index)) {
            int32 pointIndex = 0;
            queue->addPoint(0, change.value, pointIndex);
        }
        read = (read + 1) % kRingSize;
    }
    mRingRead.store(read, std::memory_order_release);
}

//------------------------------------------------------------------------
// RT thread. Only writes into the ring — the controller is a UI-thread object
// and must never be called from here. A full ring drops the oldest changes
// rather than blocking; they are pad flashes and a learned note, and the UI
// drains this every 33 ms.
void JackClient::publishOutputParameters()
{
    const int32 count = mOutputChanges.getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        Vst::IParamValueQueue *queue = mOutputChanges.getParameterData(i);
        if (!queue)
            continue;
        const int32 points = queue->getPointCount();
        if (points < 1)
            continue;
        int32 offset = 0;
        Vst::ParamValue value = 0.0;
        if (queue->getPoint(points - 1, offset, value) != kResultTrue)
            continue;

        const uint32_t write = mOutRingWrite.load(std::memory_order_relaxed);
        const uint32_t next = (write + 1) % kRingSize;
        if (next == mOutRingRead.load(std::memory_order_acquire))
            return; // full
        mOutRing[write] = {queue->getParameterId(), value};
        mOutRingWrite.store(next, std::memory_order_release);
    }
}

//------------------------------------------------------------------------
// RT thread. Refills the event list with the JACK MIDI events that fall in
// [from, to), rebased so sampleOffset is relative to the chunk the processor
// is about to be handed. Without the rebasing, a JACK period larger than the
// processor's block size would push every event in the later chunks past the
// end of the block they are dispatched in.
void JackClient::collectEvents(void *midiBuffer, int32 from, int32 to)
{
    mEvents.clear();
    if (!midiBuffer)
        return;

    const jack_nframes_t count = jack_midi_get_event_count(midiBuffer);
    for (jack_nframes_t i = 0; i < count; ++i) {
        jack_midi_event_t midi;
        if (jack_midi_event_get(&midi, midiBuffer, i) != 0 || midi.size < 3)
            continue;
        const int32 time = static_cast<int32>(midi.time);
        if (time < from)
            continue;
        if (time >= to)
            break; // JACK delivers events in time order

        const uint8_t statusByte = midi.buffer[0] & 0xF0u;
        const uint8_t channel = midi.buffer[0] & 0x0Fu;
        const uint8_t data1 = midi.buffer[1] & 0x7Fu;
        const uint8_t data2 = midi.buffer[2] & 0x7Fu;
        if (statusByte != 0x90 && statusByte != 0x80)
            continue;

        Vst::Event event = {};
        event.busIndex = 0;
        event.sampleOffset = time - from;
        event.flags = Vst::Event::kIsLive;
        // A note-on with zero velocity is a note-off, as every MIDI source is
        // entitled to send it.
        if (statusByte == 0x90 && data2 > 0) {
            event.type = Vst::Event::kNoteOnEvent;
            event.noteOn.channel = channel;
            event.noteOn.pitch = data1;
            event.noteOn.velocity = static_cast<float>(data2) / 127.0f;
            event.noteOn.noteId = -1;
        } else {
            event.type = Vst::Event::kNoteOffEvent;
            event.noteOff.channel = channel;
            event.noteOff.pitch = data1;
            event.noteOff.velocity = static_cast<float>(data2) / 127.0f;
            event.noteOff.noteId = -1;
        }
        mEvents.addEvent(event); // full list drops the rest; never allocates
    }
}

//------------------------------------------------------------------------
int JackClient::processTrampoline(jack_nframes_t nframes, void *arg)
{
    return static_cast<JackClient *>(arg)->process(nframes);
}

//------------------------------------------------------------------------
// JACK's notification thread, with the process cycle suspended. It would be
// legal to do the whole reconfiguration here, but setupProcessing and
// setActive are VST3 main-thread calls and the plug-in's message thread may be
// in the middle of a load, so all this does is record the new size for the run
// loop to act on.
int JackClient::bufferSizeTrampoline(jack_nframes_t nframes, void *arg)
{
    static_cast<JackClient *>(arg)->mNewBlockSize.store(static_cast<int>(nframes),
                                                        std::memory_order_release);
    return 0;
}

//------------------------------------------------------------------------
int JackClient::takeBufferSizeChange()
{
    const int size = mNewBlockSize.exchange(0, std::memory_order_acquire);
    // JACK announces the size once at activation too; only a real move counts.
    if (size <= 0 || size == mBlockSize.load(std::memory_order_relaxed))
        return 0;
    return size;
}

//------------------------------------------------------------------------
bool JackClient::suspendProcessing()
{
    if (!mClient)
        return true; // no audio thread to race with

    mSuspended.store(true, std::memory_order_release);

    // Two cycles, not one: the first may already have been inside process()
    // when the flag went up, so only the second is guaranteed to have seen it.
    const uint32_t start = mCycle.load(std::memory_order_acquire);
    for (int attempt = 0; attempt < 200; ++attempt) { // ~2 s
        if (mCycle.load(std::memory_order_acquire) - start >= 2)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // The audio thread is not running (a stopped server, or freewheeling).
    // Say so and leave the processor alone rather than reconfiguring it
    // underneath a callback that might still come back.
    mSuspended.store(false, std::memory_order_release);
    return false;
}

//------------------------------------------------------------------------
void JackClient::resumeProcessing(int blockSize)
{
    if (blockSize > 0) {
        mBlockSize.store(blockSize, std::memory_order_relaxed);
        // UI thread, audio callback suspended: the only safe place to grow the
        // silent input block to the new chunk size.
        if (static_cast<int>(mSilence.size()) < blockSize)
            mSilence.assign(static_cast<size_t>(blockSize), 0.0f);
    }
    mSuspended.store(false, std::memory_order_release);
}

//------------------------------------------------------------------------
// JACK real-time thread. Nothing here allocates, locks, or logs.
int JackClient::process(jack_nframes_t nframes)
{
    float *outL = static_cast<float *>(jack_port_get_buffer(mOutPorts[0], nframes));
    float *outR = static_cast<float *>(jack_port_get_buffer(mOutPorts[1], nframes));
    if (!outL || !outR)
        return 0;

    // Suspended: the UI thread is reconfiguring the processor and must not be
    // raced. Silence is the honest output — a buffer-size change already puts
    // a gap in the audio flow, and stale samples would be worse.
    if (!mProcessor || mSuspended.load(std::memory_order_acquire)) {
        memset(outL, 0, nframes * sizeof(float));
        memset(outR, 0, nframes * sizeof(float));
        mCycle.fetch_add(1, std::memory_order_release);
        return 0;
    }

    void *midiBuffer = mMidiPort ? jack_port_get_buffer(mMidiPort, nframes) : nullptr;

    mInputChanges.clearQueue();
    mOutputChanges.clearQueue();
    drainParameterRing();

    // Loop, never clamp. JACK's buffer size can change under a running client,
    // and the processor was set up for mBlockSize: handing it more would break
    // that contract, and truncating to mBlockSize would leave the rest of the
    // block holding whatever JACK's buffer had in it from the previous cycle,
    // which is stale audio rather than a dropout.
    const jack_nframes_t chunk =
        static_cast<jack_nframes_t>(mBlockSize.load(std::memory_order_relaxed));
    jack_nframes_t done = 0;
    while (done < nframes) {
        const int32 n = static_cast<int32>(std::min<jack_nframes_t>(chunk, nframes - done));

        // The input bus is ignored by the DSP but has to point somewhere: the
        // silent block, which is at least `chunk` long and all zeroes.
        if (mProcessData.inputs) {
            for (int32 ch = 0; ch < mProcessData.inputs[0].numChannels; ++ch)
                mProcessData.inputs[0].channelBuffers32[ch] = mSilence.data();
        }
        // Point the VST3 output buffers straight at JACK's, so no copy is
        // needed.
        if (mProcessData.outputs && mProcessData.outputs[0].numChannels > 1) {
            mProcessData.outputs[0].channelBuffers32[0] = outL + done;
            mProcessData.outputs[0].channelBuffers32[1] = outR + done;
        }
        mProcessData.numSamples = n;

        collectEvents(midiBuffer, static_cast<int32>(done), static_cast<int32>(done) + n);

        mProcessor->process(mProcessData);

        publishOutputParameters();
        // The queued edits belong to the top of the JACK block, not to every
        // chunk of it; replaying them would re-apply the same point n times.
        mInputChanges.clearQueue();
        mOutputChanges.clearQueue();
        done += static_cast<jack_nframes_t>(n);
    }

    // Every exit from this callback bumps the cycle counter, including the
    // early ones: suspendProcessing() waits on it, and a path that skipped it
    // would look like an audio thread that had stopped responding.
    mCycle.fetch_add(1, std::memory_order_release);
    return 0;
}

} // namespace DRUMix
