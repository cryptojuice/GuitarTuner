#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

namespace GuitarTuner {

class TunerView;

class TunerController : public Steinberg::Vst::EditControllerEx1,
                         public VSTGUI::VST3EditorDelegate
{
public:
    static Steinberg::FUnknown* createInstance (void*)
    {
        return (Steinberg::Vst::IEditController*)new TunerController;
    }

    // EditController overrides
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView (const char* name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
                                                       Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

    // Parameter display
    Steinberg::tresult PLUGIN_API getParamStringByValue (Steinberg::Vst::ParamID tag,
                                                          Steinberg::Vst::ParamValue valueNormalized,
                                                          Steinberg::Vst::String128 string) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParamValueByString (Steinberg::Vst::ParamID tag,
                                                          Steinberg::Vst::TChar* string,
                                                          Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE;

    // VST3EditorDelegate
    VSTGUI::CView* verifyView (VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                                const VSTGUI::IUIDescription* description,
                                VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

    DELEGATE_REFCOUNT (EditControllerEx1)

private:
    void updateTunerView ();

    TunerView* tunerView_ = nullptr;
};

} // namespace GuitarTuner
