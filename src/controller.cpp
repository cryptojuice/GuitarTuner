#include "controller.h"
#include "plugids.h"
#include "dsp/pitch_detector.h"
#include "ui/tuner_view.h"

#include "base/source/fstreamer.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

#include <cstdio>
#include <cstdlib>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace GuitarTuner {

tresult PLUGIN_API TunerController::initialize (FUnknown* context)
{
    tresult result = EditControllerEx1::initialize (context);
    if (result != kResultOk)
        return result;

    // Tuning reference: 432-445 Hz, default 440
    parameters.addParameter (
        STR16 ("Reference"), STR16 ("Hz"), 0,
        (440.0 - 432.0) / 13.0, // default normalized = ~0.615
        ParameterInfo::kCanAutomate, kTuningReference);

    // Detected frequency (read-only output)
    parameters.addParameter (
        STR16 ("Frequency"), STR16 ("Hz"), 0, 0.0,
        ParameterInfo::kIsReadOnly, kDetectedFrequency);

    // Cents deviation (read-only output)
    parameters.addParameter (
        STR16 ("Cents"), STR16 ("ct"), 0, 0.5, // 0.5 = 0 cents
        ParameterInfo::kIsReadOnly, kCentsDeviation);

    // Detected note (read-only output)
    parameters.addParameter (
        STR16 ("Note"), nullptr, 0, 0.0,
        ParameterInfo::kIsReadOnly, kDetectedNote);

    // Confidence (read-only output)
    parameters.addParameter (
        STR16 ("Confidence"), nullptr, 0, 0.0,
        ParameterInfo::kIsReadOnly, kConfidence);

    return kResultOk;
}

tresult PLUGIN_API TunerController::terminate ()
{
    tunerView_ = nullptr;
    return EditControllerEx1::terminate ();
}

tresult PLUGIN_API TunerController::setComponentState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);

    // Read and discard legacy algorithm field for backward compatibility
    int32 legacyAlgorithm = 0;
    streamer.readInt32 (legacyAlgorithm);

    float savedReference = 440.0f;
    if (streamer.readFloat (savedReference))
    {
        ParamValue normalized = (savedReference - 432.0) / 13.0;
        if (normalized < 0.0) normalized = 0.0;
        if (normalized > 1.0) normalized = 1.0;
        setParamNormalized (kTuningReference, normalized);
    }

    return kResultOk;
}

IPlugView* PLUGIN_API TunerController::createView (const char* name)
{
    if (FIDStringsEqual (name, ViewType::kEditor))
    {
        auto* view = new VSTGUI::VST3Editor (this, "view", "tuner_editor.uidesc");
        return view;
    }
    return nullptr;
}

tresult PLUGIN_API TunerController::setState (IBStream* /*state*/)
{
    return kResultOk;
}

tresult PLUGIN_API TunerController::getState (IBStream* /*state*/)
{
    return kResultOk;
}

tresult PLUGIN_API TunerController::setParamNormalized (ParamID tag, ParamValue value)
{
    tresult result = EditControllerEx1::setParamNormalized (tag, value);
    if (result == kResultOk)
    {
        updateTunerView ();
    }
    return result;
}

tresult PLUGIN_API TunerController::getParamStringByValue (ParamID tag, ParamValue valueNormalized,
                                                             String128 string)
{
    switch (tag)
    {
        case kTuningReference:
        {
            float refHz = 432.0f + static_cast<float> (valueNormalized) * 13.0f;
            char buf[32];
            std::snprintf (buf, sizeof (buf), "%.1f", refHz);
            Steinberg::UString (string, 128).fromAscii (buf);
            return kResultTrue;
        }
        case kDetectedFrequency:
        {
            float freq = static_cast<float> (valueNormalized) * 2000.0f;
            char buf[32];
            if (freq > 0.1f)
                std::snprintf (buf, sizeof (buf), "%.1f", freq);
            else
                std::snprintf (buf, sizeof (buf), "---");
            Steinberg::UString (string, 128).fromAscii (buf);
            return kResultTrue;
        }
        case kCentsDeviation:
        {
            float cents = static_cast<float> (valueNormalized) * 100.0f - 50.0f;
            char buf[32];
            if (cents >= 0.0f)
                std::snprintf (buf, sizeof (buf), "+%.1f", cents);
            else
                std::snprintf (buf, sizeof (buf), "%.1f", cents);
            Steinberg::UString (string, 128).fromAscii (buf);
            return kResultTrue;
        }
        default:
            return EditControllerEx1::getParamStringByValue (tag, valueNormalized, string);
    }
}

tresult PLUGIN_API TunerController::getParamValueByString (ParamID tag, TChar* string,
                                                             ParamValue& valueNormalized)
{
    switch (tag)
    {
        case kTuningReference:
        {
            Steinberg::String str (string);
            str.toMultiByte (Steinberg::kCP_Utf8);
            float refHz = static_cast<float> (atof (str.text8 ()));
            valueNormalized = (refHz - 432.0) / 13.0;
            if (valueNormalized < 0.0) valueNormalized = 0.0;
            if (valueNormalized > 1.0) valueNormalized = 1.0;
            return kResultTrue;
        }
        default:
            return EditControllerEx1::getParamValueByString (tag, string, valueNormalized);
    }
}

VSTGUI::CView* TunerController::verifyView (VSTGUI::CView* view,
                                               const VSTGUI::UIAttributes& attributes,
                                               const VSTGUI::IUIDescription* description,
                                               VSTGUI::VST3Editor* editor)
{
    // Check if this is our custom TunerView
    auto* tv = dynamic_cast<TunerView*> (view);
    if (tv)
    {
        tunerView_ = tv;
        updateTunerView ();
    }
    return view;
}

void TunerController::updateTunerView ()
{
    if (!tunerView_)
        return;

    // Reference
    float refHz = 432.0f + static_cast<float> (getParamNormalized (kTuningReference)) * 13.0f;
    tunerView_->setReference (refHz);

    // Detected frequency
    float freq = static_cast<float> (getParamNormalized (kDetectedFrequency)) * 2000.0f;
    tunerView_->setFrequency (freq);

    // Cents deviation
    float cents = static_cast<float> (getParamNormalized (kCentsDeviation)) * 100.0f - 50.0f;
    tunerView_->setCents (cents);

    // Confidence
    float conf = static_cast<float> (getParamNormalized (kConfidence));
    tunerView_->setConfidence (conf);

    // Note name
    if (freq > 20.0f)
    {
        NoteInfo noteInfo = frequencyToNoteInfo (freq, refHz);
        tunerView_->setNoteName (noteInfo.noteName);
    }
    else
    {
        tunerView_->setNoteName ("-");
    }
}

Steinberg::FUnknown* createTunerControllerInstance (void* context)
{
    return (Steinberg::Vst::IEditController*)new TunerController;
}

} // namespace GuitarTuner
