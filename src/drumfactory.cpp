// DRUMix plug-in factory.

#include "drumcontroller.h"
#include "drumids.h"
#include "drumprocessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory_constexpr.h"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS(DRUMix::DrumixProcessorUID, Steinberg::PClassInfo::kManyInstances, kVstAudioEffectClass,
          stringPluginName, Steinberg::Vst::kDistributable, "Instrument|Drum", FULL_VERSION_STR,
          kVstVersionString, DRUMix::DrumProcessor::createInstance, nullptr)

DEF_CLASS(DRUMix::DrumixControllerUID, Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass, stringPluginName "Controller", 0, "", FULL_VERSION_STR,
          kVstVersionString, DRUMix::DrumController::createInstance, nullptr)

END_FACTORY
