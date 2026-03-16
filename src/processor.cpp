#include "processor.h"
#include "plugids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace GuitarTuner {

TunerProcessor::TunerProcessor ()
{
    setControllerClass (TunerControllerUID);
}

TunerProcessor::~TunerProcessor () = default;

tresult PLUGIN_API TunerProcessor::initialize (FUnknown* context)
{
    tresult result = AudioEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // Mono audio input + pass-through output
    // An output bus is required for DAW compatibility (e.g. Ableton Live rejects
    // VST3 effects that only declare an input bus).
    addAudioInput (STR16 ("Mono In"), SpeakerArr::kMono);
    addAudioOutput (STR16 ("Mono Out"), SpeakerArr::kMono);

    return kResultOk;
}

tresult PLUGIN_API TunerProcessor::terminate ()
{
    return AudioEffect::terminate ();
}

tresult PLUGIN_API TunerProcessor::setActive (TBool state)
{
    if (state)
    {
        // Allocate buffers
        analysisBuffer.resize (kAnalysisWindowSize, 0.0f);
        samplesSinceLastAnalysis = 0;
        ringBuffer.clear ();

        // Create detector
        mpmDetector = std::make_unique<MpmDetector> (0.93f);

        lastFrequency = 0.0f;
        lastCents = 0.0f;
        lastConfidence = 0.0f;
        lastNoteIndex = -1;
    }
    else
    {
        // Free buffers
        analysisBuffer.clear ();
        analysisBuffer.shrink_to_fit ();
        mpmDetector.reset ();
    }

    return AudioEffect::setActive (state);
}

tresult PLUGIN_API TunerProcessor::setupProcessing (ProcessSetup& newSetup)
{
    sampleRate = static_cast<float> (newSetup.sampleRate);
    return AudioEffect::setupProcessing (newSetup);
}

tresult PLUGIN_API TunerProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                        SpeakerArrangement* outputs, int32 numOuts)
{
    // Accept mono input + mono output
    if (numIns == 1 && numOuts == 1 &&
        inputs[0] == SpeakerArr::kMono && outputs[0] == SpeakerArr::kMono)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API TunerProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32)
        return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API TunerProcessor::process (ProcessData& data)
{
    // Read input parameter changes
    if (data.inputParameterChanges)
    {
        int32 numParamsChanged = data.inputParameterChanges->getParameterCount ();
        for (int32 i = 0; i < numParamsChanged; i++)
        {
            IParamValueQueue* paramQueue = data.inputParameterChanges->getParameterData (i);
            if (paramQueue)
            {
                ParamValue value;
                int32 sampleOffset;
                int32 numPoints = paramQueue->getPointCount ();
                if (paramQueue->getPoint (numPoints - 1, sampleOffset, value) == kResultTrue)
                {
                    switch (paramQueue->getParameterId ())
                    {
                        case kTuningReference:
                            // Normalize from [0,1] to [432,445]
                            tuningReference.store (432.0f + static_cast<float> (value) * 13.0f, std::memory_order_relaxed);
                            break;
                    }
                }
            }
        }
    }

    // Process audio input
    if (data.numInputs == 0 || data.numSamples == 0)
        return kResultOk;

    float** input = data.inputs[0].channelBuffers32;
    if (!input || !input[0])
        return kResultOk;

    const float* audioIn = input[0];
    int32 numSamples = data.numSamples;

    // Pass-through: copy input to output so the signal chain is not broken.
    // This is required for Ableton Live and other DAWs that expect an output bus.
    if (data.numOutputs > 0 && data.outputs[0].channelBuffers32)
    {
        float* audioOut = data.outputs[0].channelBuffers32[0];
        if (audioOut && audioOut != audioIn)
        {
            std::memcpy (audioOut, audioIn, static_cast<size_t> (numSamples) * sizeof (float));
        }
    }

    // Push audio into ring buffer
    ringBuffer.push (audioIn, static_cast<size_t> (numSamples));
    samplesSinceLastAnalysis += static_cast<size_t> (numSamples);

    // Run pitch detection when we have enough new samples (hop size)
    if (samplesSinceLastAnalysis >= kHopSize && ringBuffer.available () >= kAnalysisWindowSize)
    {
        samplesSinceLastAnalysis = 0;

        // Read the latest analysis window from ring buffer
        ringBuffer.readLatest (analysisBuffer.data (), kAnalysisWindowSize);

        PitchDetector* detector = mpmDetector.get ();
        if (detector)
        {
            float refA4 = tuningReference.load (std::memory_order_relaxed);
            PitchResult result = detector->detect (analysisBuffer.data (), kAnalysisWindowSize, sampleRate);

            if (result.frequency > 0.0f)
            {
                // Recompute note info with current reference
                NoteInfo noteInfo = frequencyToNoteInfo (result.frequency, refA4);
                lastFrequency = result.frequency;
                lastCents = noteInfo.cents;
                lastConfidence = result.confidence;
                lastNoteIndex = noteInfo.midiNote;
            }
            else
            {
                lastFrequency = 0.0f;
                lastCents = 0.0f;
                lastConfidence = 0.0f;
                lastNoteIndex = -1;
            }
        }
    }

    // Write output parameter changes
    if (data.outputParameterChanges)
    {
        int32 index = 0;

        // Detected frequency
        IParamValueQueue* freqQueue = data.outputParameterChanges->addParameterData (kDetectedFrequency, index);
        if (freqQueue)
        {
            // Normalize to [0,1] range: 0-2000 Hz
            ParamValue normFreq = static_cast<ParamValue> (lastFrequency) / 2000.0;
            if (normFreq > 1.0) normFreq = 1.0;
            if (normFreq < 0.0) normFreq = 0.0;
            freqQueue->addPoint (0, normFreq, index);
        }

        // Cents deviation
        IParamValueQueue* centsQueue = data.outputParameterChanges->addParameterData (kCentsDeviation, index);
        if (centsQueue)
        {
            // Normalize from [-50,50] to [0,1]
            ParamValue normCents = (static_cast<ParamValue> (lastCents) + 50.0) / 100.0;
            if (normCents > 1.0) normCents = 1.0;
            if (normCents < 0.0) normCents = 0.0;
            centsQueue->addPoint (0, normCents, index);
        }

        // Confidence
        IParamValueQueue* confQueue = data.outputParameterChanges->addParameterData (kConfidence, index);
        if (confQueue)
        {
            confQueue->addPoint (0, static_cast<ParamValue> (lastConfidence), index);
        }

        // Detected note (MIDI note number normalized)
        IParamValueQueue* noteQueue = data.outputParameterChanges->addParameterData (kDetectedNote, index);
        if (noteQueue)
        {
            ParamValue normNote = lastNoteIndex >= 0 ? static_cast<ParamValue> (lastNoteIndex) / 127.0 : 0.0;
            noteQueue->addPoint (0, normNote, index);
        }
    }

    return kResultOk;
}

tresult PLUGIN_API TunerProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);

    // Read and discard legacy algorithm field for backward compatibility
    int32 legacyAlgorithm = 0;
    if (!streamer.readInt32 (legacyAlgorithm))
        return kResultFalse;

    float savedReference = 440.0f;
    if (!streamer.readFloat (savedReference))
        return kResultFalse;
    tuningReference.store (savedReference, std::memory_order_relaxed);

    return kResultOk;
}

tresult PLUGIN_API TunerProcessor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);

    // Write dummy algorithm field for backward compatibility
    streamer.writeInt32 (1); // MPM
    streamer.writeFloat (tuningReference.load (std::memory_order_relaxed));

    return kResultOk;
}

Steinberg::FUnknown* createTunerProcessorInstance (void* context)
{
    return (Steinberg::Vst::IAudioProcessor*)new TunerProcessor;
}

} // namespace GuitarTuner
