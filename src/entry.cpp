// Entry point - define factory for VST3 plugin
// NOTE: We avoid including processor.h/controller.h here because VSTGUI headers
// introduce Steinberg::String which conflicts with the factory macros.

#include "plugids.h"
#include "version.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"

// Forward-declare createInstance functions
namespace GuitarTuner {
    Steinberg::FUnknown* createTunerProcessorInstance (void*);
    Steinberg::FUnknown* createTunerControllerInstance (void*);
}

#define stringPluginName "Guitar Tuner"

SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory ()
{
    if (!Steinberg::gPluginFactory)
    {
        static Steinberg::PFactoryInfo factoryInfo (
            stringCompanyName, stringCompanyWeb, stringCompanyEmail,
            Steinberg::PFactoryInfo::kUnicode);
        Steinberg::gPluginFactory = new Steinberg::CPluginFactory (factoryInfo);

        // Register processor
        {
            static Steinberg::PClassInfo2 processorClass (
                GuitarTuner::TunerProcessorUID.toTUID (),
                Steinberg::PClassInfo::kManyInstances,
                kVstAudioEffectClass,
                stringPluginName,
                Steinberg::Vst::kDistributable,
                GuitarTunerVST3Category,
                nullptr,
                FULL_VERSION_STR,
                kVstVersionString);
            Steinberg::gPluginFactory->registerClass (&processorClass,
                GuitarTuner::createTunerProcessorInstance);
        }

        // Register controller
        {
            static Steinberg::PClassInfo2 controllerClass (
                GuitarTuner::TunerControllerUID.toTUID (),
                Steinberg::PClassInfo::kManyInstances,
                kVstComponentControllerClass,
                stringPluginName "Controller",
                0,
                "",
                nullptr,
                FULL_VERSION_STR,
                kVstVersionString);
            Steinberg::gPluginFactory->registerClass (&controllerClass,
                GuitarTuner::createTunerControllerInstance);
        }
    }
    else
    {
        Steinberg::gPluginFactory->addRef ();
    }
    return Steinberg::gPluginFactory;
}
