#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "dsp/ring_buffer.h"
#include "dsp/pitch_detector.h"
#include "dsp/mpm_detector.h"
#include <atomic>
#include <vector>
#include <memory>

namespace GuitarTuner {

class TunerProcessor : public Steinberg::Vst::AudioEffect
{
public:
    TunerProcessor ();
    ~TunerProcessor () override;

    static Steinberg::FUnknown* createInstance (void*)
    {
        return (Steinberg::Vst::IAudioProcessor*)new TunerProcessor;
    }

    // AudioEffect overrides
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
                                                       Steinberg::int32 numIns,
                                                       Steinberg::Vst::SpeakerArrangement* outputs,
                                                       Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

    // State persistence
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    // DSP
    static constexpr size_t kRingBufferCapacity = 8192;
    static constexpr size_t kAnalysisWindowSize = 2048;
    static constexpr size_t kHopSize = 512;

    RingBuffer<float> ringBuffer {kRingBufferCapacity};
    std::vector<float> analysisBuffer;
    size_t samplesSinceLastAnalysis = 0;

    std::unique_ptr<MpmDetector> mpmDetector;
    std::atomic<float> tuningReference {440.0f};

    // Last detected values (for output parameters)
    float lastFrequency = 0.0f;
    float lastCents = 0.0f;
    float lastConfidence = 0.0f;
    int lastNoteIndex = -1;

    float sampleRate = 44100.0f;
};

} // namespace GuitarTuner
