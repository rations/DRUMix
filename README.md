# DRUMix

DRUMix is a **MIDI drum sampler** plug-in for Linux. Load a `.wav` into each pad
of an electronic kit, bind each pad to a MIDI note, and play it from a drum
controller or a sequencer.

It is a **raw VST3**: written directly against the
[VST 3 SDK](https://github.com/steinbergmedia/vst3sdk), embedding its editor
into the host window through `IPlugView` and painting that editor itself with
Cairo and FreeType. There is no plug-in framework and no GUI toolkit in the
build — no JUCE, no iPlug2, no VSTGUI, no GTK or Qt — which is what lets DRUMix
stay **MIT-licensed**. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

DRUMix is the Linux port of DRUMku, a native Haiku VST3 by the same author. The
sampler engine, the WAV reader and the state format came across unchanged; the
editor was rewritten against X11 and Cairo.

DRUMix ships as two binaries:

| Binary | Use |
|---|---|
| `DRUMix.vst3` | VST3 plug-in — load inside a DAW (REAPER, Ardour, Bitwig, Carla, …) |
| `drumix-standalone` | Standalone application — runs without a DAW, connects directly to JACK |

---

## The editor

Nine pads, mapped to the first nine slots: kick, snare, two rack toms and a
floor tom as hex pads, hi-hat, two crashes and a ride as cymbal pads.

Click a pad to select it, then use the strip below:

- **Load…** opens a `.wav` browser drawn inside the panel (nothing links a file
  dialog toolkit), **Clear** empties the slot.
- **Learn** arms MIDI learn: hit the pad on your kit and that note binds to the
  selected slot. Click Learn again to cancel.
- **Volume** sets the slot's level. Drag it, or use the mouse wheel over it.

Pads flash when their note fires, and the armed pad pulses while it is waiting
for one. Samples are resampled to the host's rate when they are loaded, so a
44.1 kHz kit plays at the right pitch in a 48 kHz session.

### The kit rack

Nine pads is not a whole kit for everyone, so **Expand kit** opens the rack: one
row per slot, each with the same Load / Clear / Learn / volume as the strip.
**+ Add slot** shows another row — up to 64 — and **− Remove slot** hides the
last one. The mouse wheel scrolls the list, or edits a row's volume when the
pointer is over it.

Hiding a row only takes it off the list: its sample, note and volume are kept,
and it still plays. The row count is saved with the project.

The plug-in exposes Volume and Note per slot as ordinary VST3 parameters, so a
host can automate them and its generic panel can reach all 64 slots. The sample
path travels separately, over the plug-in's own `IDrumLoader` interface, because
a VST3 parameter cannot carry a string.

## System requirements

The plug-in needs Cairo, FreeType, fontconfig and libX11 at runtime — all
present on any desktop Linux install. It does **not** link JACK.

The standalone additionally needs the JACK client library (`libjack.so.0`) and a
running JACK server — `jackd2` on Debian/Devuan/Ubuntu, or `pipewire-jack` on a
PipeWire desktop. Both ship the library, so if you already run JACK you already
have it.

## Building from source

```sh
git clone --recurse-submodules https://github.com/rations/DRUMix
cd DRUMix
make            # cmake + ninja
make install    # copies DRUMix.vst3 into ~/.vst3
```

Build dependencies: CMake ≥ 3.25, Ninja, a C++17 compiler, and the development
packages for cairo, freetype2, fontconfig and libX11 (plus JACK's, for the
standalone). Only four of the SDK superproject's submodules are needed:

```sh
git submodule update --init vst3sdk
git -C vst3sdk submodule update --init base cmake pluginterfaces public.sdk
```

Other useful targets:

```sh
make validate   # run the SDK validator against the installed bundle
make render     # render the editor to a PNG with no X server
```

## Licence

MIT. See [LICENSE](LICENSE), and [NOTICE](NOTICE) for third-party attribution.
VST is a trademark of Steinberg Media Technologies GmbH.
