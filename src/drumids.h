// DRUMix — a native VST3 MIDI drum sampler for Linux.
//
// A drum rack: each slot loads a .wav sample, has a volume, and is bound to a
// MIDI note. Incoming note-on events trigger the sample(s) bound to that pitch.
// Written against the VST3 SDK only — no plug-in framework.

#pragma once

#include "idrumloader.h" // shared parameter-ID layout + IDrumLoader interface

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace DRUMix
{

// Message IDs for controller -> processor sample loading (IConnectionPoint).
// Attribute "slot" (setInt) selects the slot; attribute "path" (setBinary)
// carries a UTF-8 byte string (empty = clear the slot).
inline constexpr const char *kMsgLoadSample = "DRUMixLoadSample";
inline constexpr const char *kSlotAttr = "slot";
inline constexpr const char *kPathAttr = "path";

// Controller -> processor MIDI-learn arming (IConnectionPoint). Attribute
// "slot" (setInt) selects the slot to learn; -1 disarms. The processor
// captures the next note-on, binds it to the slot, and reports it back as an
// output parameter change on (kSlotNoteBase + slot).
inline constexpr const char *kMsgArmLearn = "DRUMixArmLearn";

static DECLARE_UID(DrumixProcessorUID, 0xA88BD0F9, 0xB7439A47, 0xA34AE23E, 0x6A8700F6);
static DECLARE_UID(DrumixControllerUID, 0x160399E3, 0xF60E09CA, 0x7F8CCE55, 0x944C3F0F);

} // namespace DRUMix
