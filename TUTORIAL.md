# Building a Guitar Tuner VST3 Plugin from Scratch

**A step-by-step guide for developers new to C++ and audio plugin development**

You want to build an audio plugin. Maybe you're a guitarist who codes, or a developer who plays guitar. Either way, you've landed on one of the best beginner projects in audio programming: a chromatic guitar tuner.

By the end of this tutorial, you'll have a working VST3 plugin that:
- Captures audio from your guitar in real time
- Detects the pitch using two different algorithms (YIN and MPM)
- Displays a smooth needle gauge showing how in-tune you are
- Works in Ableton Live, Cubase, REAPER, and any other VST3 host

No prior C++ or audio DSP experience required. We'll explain everything as we go.

---

## Table of Contents

1. [What is a VST3 Plugin?](#1-what-is-a-vst3-plugin)
2. [Prerequisites and Tools](#2-prerequisites-and-tools)
3. [Project Setup](#3-project-setup)
4. [Understanding the VST3 Architecture](#4-understanding-the-vst3-architecture)
5. [Step 1: Plugin Identity (plugids.h, version.h)](#5-step-1-plugin-identity)
6. [Step 2: The Audio Processor](#6-step-2-the-audio-processor)
7. [Step 3: Pitch Detection - How Does a Tuner Actually Work?](#7-step-3-pitch-detection)
8. [Step 4: The YIN Algorithm](#8-step-4-the-yin-algorithm)
9. [Step 5: The MPM Algorithm](#9-step-5-the-mpm-algorithm)
10. [Step 6: The Lock-Free Ring Buffer](#10-step-6-the-lock-free-ring-buffer)
11. [Step 7: The Controller and Parameters](#11-step-7-the-controller-and-parameters)
12. [Step 8: The Tuner UI with VSTGUI](#12-step-8-the-tuner-ui-with-vstgui)
13. [Step 9: The Plugin Entry Point](#13-step-9-the-plugin-entry-point)
14. [Step 10: CMake Build Configuration](#14-step-10-cmake-build-configuration)
15. [Step 11: Building and Testing](#15-step-11-building-and-testing)
16. [Gotchas and Lessons Learned](#16-gotchas-and-lessons-learned)
17. [Where to Go from Here](#17-where-to-go-from-here)

---

## 1. What is a VST3 Plugin?

VST3 (Virtual Studio Technology 3) is Steinberg's plugin format for audio software. When you open a plugin like a reverb or compressor in your DAW, you're using a VST (or AU, or AAX) plugin.

A VST3 plugin is a shared library (a `.vst3` file, which is really a `.dll` on Windows) that your DAW loads. The DAW sends audio data into the plugin, and the plugin can process it and send it back. The plugin can also have a GUI window.

For a tuner, we receive audio, analyze the pitch, and display the result. We don't modify the audio -- we just pass it through unchanged.

---

## 2. Prerequisites and Tools

You'll need:

- **Windows 10/11** (this tutorial targets Windows, but the code is cross-platform)
- **Visual Studio 2022 or later** (Community edition is free)
- **CMake 3.19+** ([cmake.org](https://cmake.org/download/))
- **Git** ([git-scm.com](https://git-scm.com/))
- A **DAW** for testing (Ableton Live, REAPER, Cubase, etc.)

---

## 3. Project Setup

### Clone the VST3 SDK

First, create your project folder and grab the SDK:

```bash
mkdir GuitarTuner && cd GuitarTuner
git init
mkdir -p extern
git clone --recursive https://github.com/steinbergmedia/vst3sdk.git extern/vst3sdk
```

The `--recursive` flag is critical -- the SDK has submodules (vstgui, pluginterfaces, etc.) that won't work without it.

### Create the folder structure

```
GuitarTuner/
  extern/vst3sdk/          <-- the SDK (cloned above)
  src/
    dsp/                   <-- pitch detection algorithms
    ui/                    <-- custom GUI view
    plugids.h              <-- unique IDs for our plugin
    version.h              <-- version strings
    processor.h/.cpp       <-- audio processing
    controller.h/.cpp      <-- parameter management + GUI
    entry.cpp              <-- plugin factory (DLL entry point)
  resources/
    tuner_editor.uidesc    <-- GUI layout (XML)
    win32resource.rc       <-- Windows version info
  tests/
    test_pitch.cpp         <-- unit tests for our algorithms
  CMakeLists.txt           <-- build configuration
```

### .gitignore

```
build/
extern/
*.user
.vs/
```

We ignore `extern/` because it's a separate git clone and `build/` because it's generated.

---

## 4. Understanding the VST3 Architecture

Before writing code, you need to understand VST3's split architecture. Every VST3 plugin has two halves:

```
                    +-----------------+
  Audio Thread ---> |   PROCESSOR     |  Receives audio, does DSP, writes output parameters
                    +-----------------+
                           |
                    (parameter sync)
                           |
                    +-----------------+
  GUI Thread ----> |   CONTROLLER    |  Manages parameters, creates/updates the GUI
                    +-----------------+
```

**Processor** (`AudioEffect`): Lives on the real-time audio thread. Receives audio buffers 64-512 samples at a time. Must NEVER allocate memory, lock mutexes, or do anything that could block. This is the "hot path."

**Controller** (`EditController`): Lives on the GUI thread. Registers parameters (algorithm, tuning reference), creates the editor window, converts parameter values to display strings.

They communicate through the DAW via parameter changes -- the processor can't directly call the controller or vice versa.

**Why the split?** Steinberg designed VST3 so the processor could theoretically run on a different machine than the controller (network-distributed mixing). In practice, they run in the same process, but the separation enforces good real-time coding habits.

---

## 5. Step 1: Plugin Identity

Every VST3 plugin needs globally unique IDs so DAWs can tell plugins apart. We define these in `plugids.h`.

### src/plugids.h

```cpp
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace GuitarTuner {

// Processor UID - a globally unique 128-bit ID
// Generate your own at https://www.guidgenerator.com/
static const Steinberg::FUID TunerProcessorUID (
    0x1A2B3C4D, 0x5E6F7081, 0x92A3B4C5, 0xD6E7F800);

// Controller UID - must be different from processor
static const Steinberg::FUID TunerControllerUID (
    0x2B3C4D5E, 0x6F708192, 0xA3B4C5D6, 0xE7F80011);

// Parameter IDs - each automatable parameter needs a unique ID
enum ParameterIDs : Steinberg::Vst::ParamID
{
    kAlgorithmSelector = 0,   // 0=YIN, 1=MPM
    kTuningReference   = 1,   // 432-445 Hz, default 440
    kDetectedFrequency = 2,   // read-only output
    kCentsDeviation    = 3,   // read-only output (-50 to +50)
    kDetectedNote      = 4,   // read-only output
    kConfidence        = 5    // read-only output (0.0-1.0)
};

// VST3 subcategory - tells the DAW where to list the plugin
#define GuitarTunerVST3Category "Fx|Analyzer"

} // namespace GuitarTuner
```

**Key concepts for C++ newcomers:**

- `#pragma once` prevents the file from being included twice (a C++ header guard).
- `namespace GuitarTuner { ... }` is like a package in Java/Python -- it prevents our names from colliding with other code.
- `static const` means "one copy per translation unit, can't be changed."
- `enum ParameterIDs` defines named integer constants. The `: Steinberg::Vst::ParamID` part says our enum values are the same type as VST3's parameter IDs.
- `#define GuitarTunerVST3Category "Fx|Analyzer"` is a preprocessor macro. Unlike `static const`, macros are simple text replacement. We use a macro here because of how the VST3 factory registration works (more on that later).

### src/version.h

```cpp
#pragma once

#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"  // Auto-generated by CMake

#define stringOriginalFilename  "GuitarTuner.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "Guitar Tuner VST3 (64Bit)"
#else
#define stringFileDescription   "Guitar Tuner VST3"
#endif
#define stringCompanyWeb        "https://github.com/guitartuner"
#define stringCompanyEmail      "mailto:info@guitartuner.dev"
#define stringCompanyName       "GuitarTuner"
#define stringLegalCopyright    "Copyright 2026 GuitarTuner"
#define stringLegalTrademarks   "VST is a trademark of Steinberg Media Technologies GmbH"
```

This is boilerplate. CMake generates `projectversion.h` with the version number.

---

## 6. Step 2: The Audio Processor

The processor is the heart of the plugin. Let's walk through it piece by piece.

### src/processor.h

```cpp
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "dsp/ring_buffer.h"
#include "dsp/pitch_detector.h"
#include "dsp/yin_detector.h"
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

    // ... (override declarations for initialize, process, etc.)

private:
    // DSP constants
    static constexpr size_t kRingBufferCapacity = 8192;
    static constexpr size_t kAnalysisWindowSize = 2048;
    static constexpr size_t kHopSize = 512;

    RingBuffer<float> ringBuffer {kRingBufferCapacity};
    std::vector<float> analysisBuffer;
    size_t samplesSinceLastAnalysis = 0;

    std::unique_ptr<YinDetector> yinDetector;
    std::unique_ptr<MpmDetector> mpmDetector;

    std::atomic<int> activeAlgorithm {0};       // 0=YIN, 1=MPM
    std::atomic<float> tuningReference {440.0f};

    float lastFrequency = 0.0f;
    float lastCents = 0.0f;
    float lastConfidence = 0.0f;
    int lastNoteIndex = -1;

    float sampleRate = 44100.0f;
};

} // namespace GuitarTuner
```

**Why these constants?**

- **2048-sample analysis window**: At 44.1 kHz, this gives us 46ms of audio. A guitar's low E string (82 Hz) has a period of ~12ms, so 2048 samples captures ~4 full cycles -- enough for reliable detection.
- **512-sample hop size**: We re-analyze every 512 samples (~11ms), giving responsive but not wasteful updates.
- **8192-sample ring buffer**: Power-of-2 size for efficient modular arithmetic. Holds ~4 analysis windows as a safety margin.

**Why `std::atomic`?** The `activeAlgorithm` and `tuningReference` variables are written by the parameter-change mechanism and read by the audio processing code. `std::atomic` ensures these reads/writes are thread-safe without locks -- critical for real-time audio where you must never block.

**Why `std::unique_ptr`?** Smart pointers automatically free memory when they go out of scope. We allocate detectors when the plugin activates and free them when it deactivates.

### src/processor.cpp - The Process Function

The `process()` function is called by the DAW on every audio block (typically 64-512 samples). Here's what happens:

```cpp
tresult PLUGIN_API TunerProcessor::process (ProcessData& data)
{
    // 1. READ PARAMETER CHANGES from the DAW
    if (data.inputParameterChanges) { /* ... */ }

    // 2. GET THE AUDIO BUFFER
    const float* audioIn = data.inputs[0].channelBuffers32[0];

    // 3. PASS-THROUGH: copy input to output (required by Ableton)
    if (data.numOutputs > 0)
        std::memcpy (audioOut, audioIn, numSamples * sizeof(float));

    // 4. FEED THE RING BUFFER
    ringBuffer.push (audioIn, numSamples);
    samplesSinceLastAnalysis += numSamples;

    // 5. RUN PITCH DETECTION when we have enough new samples
    if (samplesSinceLastAnalysis >= kHopSize &&
        ringBuffer.available () >= kAnalysisWindowSize)
    {
        ringBuffer.readLatest (analysisBuffer.data(), kAnalysisWindowSize);
        PitchResult result = detector->detect (...);
        // store results...
    }

    // 6. WRITE OUTPUT PARAMETERS back to the DAW
    if (data.outputParameterChanges) { /* ... */ }

    return kResultOk;
}
```

**The parameter normalization pattern**: VST3 parameters are always 0.0-1.0 internally. We must convert:
- Algorithm: 0.0 = YIN, 1.0 = MPM (split at 0.5)
- Tuning reference: 0.0 = 432 Hz, 1.0 = 445 Hz (linear map)
- Frequency output: 0.0 = 0 Hz, 1.0 = 2000 Hz
- Cents: 0.0 = -50 cents, 1.0 = +50 cents

**The pass-through**: Our tuner doesn't modify audio, but we must still copy input to output. Ableton Live rejects VST3 effects that don't have an output bus, and other DAWs expect the signal chain to continue through the plugin. This was a real compatibility issue we discovered during development.

### Bus Configuration

In `initialize()`, we declare our audio buses:

```cpp
addAudioInput (STR16 ("Mono In"), SpeakerArr::kMono);
addAudioOutput (STR16 ("Mono Out"), SpeakerArr::kMono);
```

And in `setBusArrangements()`, we tell the DAW we only accept mono in + mono out:

```cpp
if (numIns == 1 && numOuts == 1 &&
    inputs[0] == SpeakerArr::kMono && outputs[0] == SpeakerArr::kMono)
    return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
return kResultFalse;
```

---

## 7. Step 3: Pitch Detection - How Does a Tuner Actually Work?

Before looking at code, let's understand the math.

### The Core Idea: Autocorrelation

A guitar string vibrates at a fundamental frequency (the pitch) plus overtones (harmonics). If you take a snippet of audio and compare it to a time-shifted copy of itself, the correlation will peak when the shift equals exactly one period of the fundamental frequency.

```
Original:   ∿∿∿∿∿∿∿∿∿∿∿∿
Shifted by T: ∿∿∿∿∿∿∿∿∿∿∿∿   <-- lines up perfectly!
Shifted by T/2: ∿∿∿∿∿∿∿∿∿∿∿∿   <-- anti-correlated
```

The shift amount (called **tau** or **lag**) that produces the best correlation tells us the period `T`. The frequency is simply `sampleRate / T`.

Both YIN and MPM are refinements of this basic autocorrelation idea, designed to handle real-world signals reliably.

### The Shared Interface

Both detectors implement a common abstract base class:

```cpp
// src/dsp/pitch_detector.h

struct PitchResult
{
    float frequency;    // Hz, 0 if no pitch detected
    float confidence;   // 0.0-1.0
};

class PitchDetector
{
public:
    virtual ~PitchDetector () = default;
    virtual PitchResult detect (const float* audio,
                                 size_t numSamples,
                                 float sampleRate) = 0;
};
```

In C++, `virtual` means "subclasses can override this" and `= 0` means "subclasses MUST override this" (pure virtual). This is equivalent to an abstract method in Java or a protocol in Swift.

This file also contains `frequencyToNoteInfo()` -- a helper that converts a frequency like 441.5 Hz into "A4 +5.9 cents." It uses the formula:

```
midiNote = 69 + 12 * log2(frequency / referenceA4)
```

MIDI note 69 is A4. Each semitone is a factor of 2^(1/12) in frequency. The fractional part, multiplied by 100, gives the cents deviation.

---

## 8. Step 4: The YIN Algorithm

YIN (de Cheveigne & Kawahara, 2002) is the workhorse of pitch detection. It's simple, robust, and handles guitar signals well.

### The Four Steps

**Step 1 - Difference Function**: For each lag `tau`, compute how different the signal is from a shifted version of itself:

```
d(tau) = sum of (x[j] - x[j + tau])^2    for j = 0 to W/2
```

When `tau` equals the period, `x[j]` and `x[j+tau]` are nearly identical, so `d(tau)` is near zero.

**Step 2 - Cumulative Mean Normalization (CMNDF)**: Raw `d(tau)` has a bias -- it tends to decrease as tau increases (because fewer samples overlap). CMNDF fixes this:

```
d'(tau) = d(tau) / ((1/tau) * sum of d(1)...d(tau))
```

This normalizes each value by the running average, so dips are truly significant.

**Step 3 - Absolute Threshold**: Find the first `tau` where `d'(tau)` drops below a threshold (we use 0.10). Then walk forward to find the exact local minimum. This "first below threshold" rule prevents octave errors -- without it, the algorithm might pick the second harmonic.

**Step 4 - Parabolic Interpolation**: The true period is usually between sample positions. Fit a parabola through the three points around the minimum to get sub-sample accuracy:

```
adjustment = (s2 - s0) / (2 * (2*s1 - s2 - s0))
refinedTau = tau + adjustment
```

### The Code

```cpp
// src/dsp/yin_detector.cpp

void YinDetector::differenceFunction (const float* audio, size_t numSamples)
{
    for (size_t tau = 0; tau < halfWindowSize_; tau++)
    {
        float sum = 0.0f;
        for (size_t j = 0; j < halfWindowSize_; j++)
        {
            float diff = audio[j] - audio[j + tau];
            sum += diff * diff;
        }
        yinBuffer_[tau] = sum;
    }
}

void YinDetector::cumulativeMeanNormalizedDifference ()
{
    yinBuffer_[0] = 1.0f;  // d'(0) = 1 by definition
    float runningSum = 0.0f;

    for (size_t tau = 1; tau < halfWindowSize_; tau++)
    {
        runningSum += yinBuffer_[tau];
        if (runningSum != 0.0f)
            yinBuffer_[tau] = yinBuffer_[tau] * (float)tau / runningSum;
        else
            yinBuffer_[tau] = 1.0f;
    }
}
```

**Performance note**: The difference function is O(N^2) -- for each of the N/2 lag values, we sum over N/2 samples. With N=2048, that's ~500K multiplications per analysis frame. At our 86 Hz hop rate (44100/512), this is ~43M operations/second. Modern CPUs handle this easily, but for larger windows you'd want FFT-based autocorrelation.

---

## 9. Step 5: The MPM Algorithm

MPM (McLeod Pitch Method) is an alternative to YIN that uses the Normalized Square Difference Function (NSDF). It's often more accurate on clean signals.

### How MPM Differs from YIN

Instead of looking for dips in a difference function, MPM looks for **peaks** in a normalized autocorrelation:

```
nsdf(tau) = 2 * r(tau) / (energy1 + energy2)
```

Where `r(tau)` is the autocorrelation and `energy1`, `energy2` are energy terms that normalize the result to [-1, 1]. A perfect match gives nsdf = 1.0.

### Key Maxima Selection

The NSDF oscillates between positive and negative regions. MPM finds the **peak in each positive region** (called "key maxima"), then selects the first one that exceeds 93% of the highest peak. This elegantly avoids octave errors.

### The Critical Bug We Fixed

Our initial implementation had a subtle bug: the NSDF is always 1.0 at tau=0 (a signal is perfectly correlated with itself). The first positive region includes this trivial peak. If you don't skip past it, you'll always select tau near 0, giving an absurdly high frequency.

The fix: skip ahead until the NSDF goes negative for the first time, THEN start looking for peaks:

```cpp
// Skip the trivial peak at tau=0
size_t startIdx = 1;
while (startIdx < nsdf_.size() - 1 && nsdf_[startIdx] > 0.0f)
    startIdx++;

// Now find real peaks
for (size_t i = startIdx; i < nsdf_.size() - 1; i++) { ... }
```

We also limit NSDF computation to `numSamples / 2` because large lag values have too few overlapping samples to produce reliable results.

---

## 10. Step 6: The Lock-Free Ring Buffer

Audio processing has a hard real-time constraint: the `process()` function must return within microseconds. You cannot allocate memory, lock mutexes, or do I/O.

But we need to accumulate samples across multiple `process()` calls (the DAW might give us 64 samples at a time, but we need 2048 for analysis). A ring buffer solves this.

```cpp
// src/dsp/ring_buffer.h

template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer (size_t capacity)  // must be power of 2
        : capacity_(capacity), mask_(capacity - 1),
          buffer_(new T[capacity]) {}

    void push (const T* data, size_t count)
    {
        size_t w = writeHead_.load (std::memory_order_relaxed);
        for (size_t i = 0; i < count; i++)
        {
            buffer_[w & mask_] = data[i];  // wrap using bitmask
            w++;
        }
        writeHead_.store (w, std::memory_order_release);
    }

    void readLatest (T* dest, size_t count) const
    {
        size_t w = writeHead_.load (std::memory_order_acquire);
        size_t start = w - count;
        for (size_t i = 0; i < count; i++)
            dest[i] = buffer_[(start + i) & mask_];
    }

private:
    size_t capacity_, mask_;
    T* buffer_;
    std::atomic<size_t> writeHead_ {0};
};
```

**Why power-of-2?** The bitmask trick: `index & mask_` is equivalent to `index % capacity_` but much faster (one CPU instruction vs. a division). With capacity 8192 and mask 8191 (0x1FFF), index 8193 wraps to index 1.

**Why `std::atomic`?** The write head is modified by `push()` (on the audio thread) and read by `readLatest()` (potentially from a different thread). `std::atomic` with `memory_order_release`/`acquire` ensures the reader sees all the data the writer stored, without any locking.

---

## 11. Step 7: The Controller and Parameters

The controller manages the plugin's parameters and GUI.

### src/controller.h

```cpp
class TunerController : public Steinberg::Vst::EditControllerEx1,
                         public VSTGUI::VST3EditorDelegate
{
    // ...
    VSTGUI::CView* verifyView (VSTGUI::CView* view, ...);
    void updateTunerView ();
    TunerView* tunerView_ = nullptr;
};
```

It inherits from two classes (C++ supports multiple inheritance):
- `EditControllerEx1`: Steinberg's extended controller with parameter management
- `VST3EditorDelegate`: Callback interface for VSTGUI editor events

### Parameter Registration

In `initialize()`, we register six parameters:

```cpp
// Algorithm: a dropdown list
auto* algoParam = new StringListParameter (
    STR16("Algorithm"), kAlgorithmSelector, nullptr,
    ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
algoParam->appendString (STR16("YIN"));
algoParam->appendString (STR16("MPM"));
parameters.addParameter (algoParam);

// Reference: a continuous knob (432-445 Hz)
parameters.addParameter (
    STR16("Reference"), STR16("Hz"), 0,
    (440.0 - 432.0) / 13.0,  // default normalized value
    ParameterInfo::kCanAutomate, kTuningReference);

// Read-only outputs (the processor writes these)
parameters.addParameter (
    STR16("Frequency"), STR16("Hz"), 0, 0.0,
    ParameterInfo::kIsReadOnly, kDetectedFrequency);
// ... cents, note, confidence similarly
```

### Displaying Human-Readable Values

VST3 parameters are stored as 0.0-1.0 normalized values. `getParamStringByValue()` converts them for display:

```cpp
case kTuningReference:
{
    float refHz = 432.0f + valueNormalized * 13.0f;
    snprintf (buf, sizeof(buf), "%.1f", refHz);
    // ...
}
```

### The verifyView Callback

When VSTGUI creates our custom TunerView from the `.uidesc` layout, the controller gets a chance to grab a pointer to it:

```cpp
VSTGUI::CView* TunerController::verifyView (VSTGUI::CView* view, ...)
{
    auto* tv = dynamic_cast<TunerView*>(view);
    if (tv)
    {
        tunerView_ = tv;
        updateTunerView ();  // push current values to the view
    }
    return view;
}
```

`dynamic_cast` is C++'s safe downcast -- it returns `nullptr` if the view isn't actually a TunerView.

---

## 12. Step 8: The Tuner UI with VSTGUI

VSTGUI is Steinberg's cross-platform GUI framework included with the VST3 SDK. We use it in two parts: a custom `CView` subclass for the needle display, and a `.uidesc` XML file for layout.

### The Custom View (tuner_view.h/.cpp)

Our `TunerView` extends `VSTGUI::CView` and overrides `draw()` to paint:

```cpp
void TunerView::draw (VSTGUI::CDrawContext* ctx)
{
    VSTGUI::CRect r = getViewSize ();
    drawBackground (ctx, r);   // dark gradient
    drawCentMarkers (ctx, r);  // tick marks in an arc
    drawTuningArc (ctx, r);    // colored zone dots
    drawNeedle (ctx, r);       // animated needle
    drawNoteDisplay (ctx, r);  // "A4" in large text
    drawInfoText (ctx, r);     // "440.0 Hz  +2.3 cents  YIN | A4=440 Hz"
}
```

**The needle**: Maps cents deviation to an angle. -50 cents = far left, 0 = straight up, +50 = far right. The needle color changes: green (<5 cents), yellow (<15), red (>15).

**Smooth animation**: We use exponential smoothing so the needle moves fluidly instead of jumping:

```cpp
smoothedCents_ += 0.15f * (targetCents_ - smoothedCents_);
```

This is updated 30 times/second by a `CVSTGUITimer`.

**Color coding**: The arc displays colored dots showing the tuning zones:
- Green center zone (< 5 cents = "in tune")
- Yellow zone (5-15 cents = "close")
- Red zone (> 15 cents = "out of tune")

### The View Factory

VSTGUI needs a factory to create our custom view from the XML layout:

```cpp
class TunerViewFactory : public VSTGUI::ViewCreatorAdapter
{
public:
    TunerViewFactory ()
    {
        VSTGUI::UIViewFactory::registerViewCreator (*this);
    }
    VSTGUI::IdStringPtr getViewName () const override { return "TunerView"; }
    VSTGUI::IdStringPtr getBaseViewName () const override { return "CView"; }
    VSTGUI::CView* create (...) const override
    {
        return new TunerView (size);
    }
};

// This static instance auto-registers when the DLL loads
static TunerViewFactory gTunerViewFactory;
```

### The Layout File (tuner_editor.uidesc)

This XML file describes the GUI layout. The DAW loads it when creating the editor:

```xml
<template class="CViewContainer" name="view" size="400, 420">
    <!-- Our custom tuner display -->
    <view class="TunerView" origin="0, 0" size="400, 350"/>

    <!-- Algorithm dropdown -->
    <view class="COptionMenu" control-tag="Algorithm"
          origin="20, 360" size="120, 24"/>

    <!-- Reference frequency knob + text input -->
    <view class="CKnob" control-tag="Reference"
          origin="235, 355" size="32, 32"/>
    <view class="CTextEdit" control-tag="Reference"
          origin="275, 360" size="55, 24"/>
</template>

<control-tags>
    <control-tag name="Algorithm" tag="0"/>
    <control-tag name="Reference" tag="1"/>
    <control-tag name="Frequency" tag="2"/>
    <control-tag name="Cents" tag="3"/>
    <control-tag name="Note" tag="4"/>
    <control-tag name="Confidence" tag="5"/>
</control-tags>
```

The `control-tag` attributes connect GUI controls to parameter IDs. When the user turns the knob, VSTGUI automatically calls `performEdit()` on the controller, which sends the parameter change to the processor.

---

## 13. Step 9: The Plugin Entry Point

Every DLL needs an entry point. For VST3, that's `GetPluginFactory()` -- the function the DAW calls to discover what's inside the plugin.

### src/entry.cpp

```cpp
#include "plugids.h"
#include "version.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"

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

        // Register processor class
        {
            static Steinberg::PClassInfo2 processorClass (
                GuitarTuner::TunerProcessorUID.toTUID (),
                Steinberg::PClassInfo::kManyInstances,
                kVstAudioEffectClass,
                stringPluginName,
                Steinberg::Vst::kDistributable,
                GuitarTunerVST3Category,
                nullptr, FULL_VERSION_STR, kVstVersionString);
            Steinberg::gPluginFactory->registerClass (
                &processorClass, GuitarTuner::createTunerProcessorInstance);
        }

        // Register controller class
        {
            static Steinberg::PClassInfo2 controllerClass (
                GuitarTuner::TunerControllerUID.toTUID (),
                Steinberg::PClassInfo::kManyInstances,
                kVstComponentControllerClass,
                stringPluginName "Controller",
                0, "", nullptr, FULL_VERSION_STR, kVstVersionString);
            Steinberg::gPluginFactory->registerClass (
                &controllerClass, GuitarTuner::createTunerControllerInstance);
        }
    }
    else
        Steinberg::gPluginFactory->addRef ();

    return Steinberg::gPluginFactory;
}
```

**Why not use the macros?** The VST3 SDK provides `BEGIN_FACTORY_DEF` / `DEF_CLASS2` / `END_FACTORY` macros for this. However, these macros internally use `using namespace Steinberg;` which conflicts with `Steinberg::String` (pulled in by VSTGUI headers). By manually writing `GetPluginFactory()`, we avoid this namespace collision.

**Why forward-declare instead of `#include`?** Including `processor.h` or `controller.h` would transitively include VSTGUI headers, re-introducing the `Steinberg::String` conflict. Forward-declaring the factory functions avoids this entirely.

---

## 14. Step 10: CMake Build Configuration

CMake is the build system. Our `CMakeLists.txt` tells it where the SDK is, what files to compile, and what libraries to link:

```cmake
cmake_minimum_required(VERSION 3.19.0)

# Point to the VST3 SDK
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk")

# SDK options
option(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES "Enable SDK examples" OFF)
option(SMTG_ENABLE_VST3_HOSTING_EXAMPLES "Enable hosting examples" OFF)
option(SMTG_ENABLE_VSTGUI_SUPPORT "Enable VSTGUI" ON)
option(SMTG_CREATE_BUNDLE_FOR_WINDOWS "Bundle for Windows" ON)

# Import SDK build system
list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
include(SMTG_VST3_SDK)

project(GuitarTuner VERSION 1.0.0.0)

# SDK setup
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()

# Enable VSTGUI
smtg_enable_vstgui_support(VSTGUI_SOURCE_DIR "${vst3sdk_SOURCE_DIR}/vstgui4")

# Create SDK library targets
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_target()
smtg_create_public_sdk_hosting_target()

# SDK tools (moduleinfotool, validator)
add_subdirectory(${vst3sdk_SOURCE_DIR}/public.sdk/samples/vst-utilities ...)
add_subdirectory(${vst3sdk_SOURCE_DIR}/public.sdk/samples/vst-hosting ...)

# Our plugin
set(tuner_sources
    src/plugids.h src/version.h
    src/processor.h src/processor.cpp
    src/controller.h src/controller.cpp
    src/entry.cpp
    src/dsp/ring_buffer.h src/dsp/pitch_detector.h
    src/dsp/yin_detector.h src/dsp/yin_detector.cpp
    src/dsp/mpm_detector.h src/dsp/mpm_detector.cpp
    src/ui/tuner_view.h src/ui/tuner_view.cpp
)

smtg_add_vst3plugin(GuitarTuner ${tuner_sources})
target_compile_features(GuitarTuner PUBLIC cxx_std_17)
target_include_directories(GuitarTuner PRIVATE src)
target_link_libraries(GuitarTuner PRIVATE sdk vstgui_support)

# Add the .uidesc layout as a plugin resource
smtg_target_add_plugin_resources(GuitarTuner RESOURCES resources/tuner_editor.uidesc)

# Unit tests (standalone executable, no SDK dependencies)
enable_testing()
add_executable(GuitarTunerTests
    tests/test_pitch.cpp
    src/dsp/yin_detector.cpp
    src/dsp/mpm_detector.cpp
)
target_include_directories(GuitarTunerTests PRIVATE src)
target_compile_features(GuitarTunerTests PUBLIC cxx_std_17)
add_test(NAME PitchDetectorTests COMMAND GuitarTunerTests)
```

**Key functions:**
- `smtg_add_vst3plugin()` creates the DLL target with the correct export symbols and bundle structure
- `smtg_target_add_plugin_resources()` copies the `.uidesc` file into the VST3 bundle
- `smtg_target_configure_version_file()` generates `projectversion.h` from the CMake version

---

## 15. Step 11: Building and Testing

### Generate and Build

```bash
# Generate the Visual Studio project
cmake -B build -G "Visual Studio 17 2022"
# (Use "Visual Studio 18 2026" if you have VS 2026)

# Build in Release mode
cmake --build build --config Release
```

The output is at `build/VST3/Release/GuitarTuner.vst3/` -- a folder bundle containing:
```
GuitarTuner.vst3/
  Contents/
    x86_64-win/
      GuitarTuner.vst3    <-- the DLL
    Resources/
      tuner_editor.uidesc  <-- the GUI layout
      moduleinfo.json      <-- auto-generated metadata
```

### Run the Validator

The SDK includes a validator tool that checks your plugin against the VST3 specification:

```bash
build/bin/Release/validator.exe build/VST3/Release/GuitarTuner.vst3
```

You should see `Result: 47 tests passed, 0 tests failed`.

### Run Unit Tests

```bash
build/bin/Release/GuitarTunerTests.exe
```

Expected output:
```
=== Guitar Tuner Pitch Detection Tests ===
--- frequencyToNoteInfo tests ---
  PASS: A4 at 440Hz (midi=69, note=A4, cents=0.00)
  PASS: E2 at 82.41Hz (midi=40, note=E2, cents=0.07)
  ...
--- YIN tests (tolerance: 5 cents) ---
  PASS: Sine E2 (low E) (82Hz) (expected=82.41Hz, got=82.41Hz, conf=1.000)
  ...
--- MPM tests (tolerance: 5 cents) ---
  PASS: Sine E2 (low E) (82Hz) (expected=82.41Hz, got=82.41Hz, conf=1.000)
  ...
=== Results: 45/45 passed, 0 failed ===
```

### Install for Your DAW

Copy the bundle to the system VST3 folder:

```bash
xcopy /E /I build\VST3\Release\GuitarTuner.vst3 "C:\Program Files\Common Files\VST3\GuitarTuner.vst3\"
```

Then rescan plugins in your DAW. In Ableton Live: Preferences > Plug-ins > Rescan (hold Alt for a deep rescan).

---

## 16. Gotchas and Lessons Learned

Building this plugin, we ran into several real-world problems that tutorials rarely mention:

### The `Steinberg::String` Namespace Conflict

The VST3 SDK's factory macros (`BEGIN_FACTORY_DEF`, `DEF_CLASS2`) inject `using namespace Steinberg;`. VSTGUI defines a `Steinberg::String` class. When both are visible, MSVC confuses the `string` token in `PClassInfo2`'s constructor with the `String` type. **Solution**: Write `GetPluginFactory()` manually instead of using macros.

### `INLINE_UID_FROM_FUID` Produces a Braced Initializer

This macro expands to `{byte, byte, byte, ...}` which is valid for aggregate initialization but NOT as a constructor argument. **Solution**: Use `myFUID.toTUID()` which returns a `const TUID&` (reference to `int8[16]`) that decays properly to a pointer parameter.

### `#define` Inside a Namespace

Our `GuitarTunerVST3Category` is a `#define` inside `namespace GuitarTuner { }`. Preprocessor macros ignore namespaces -- `#define` happens before the compiler sees namespace scoping. Writing `GuitarTuner::GuitarTunerVST3Category` expands to `GuitarTuner::"Fx|Analyzer"` which is a syntax error.

### Ableton Live Requires an Output Bus

Unlike Cubase and REAPER, Ableton Live rejects VST3 effects that only have input buses. Even though a tuner doesn't modify audio, you must declare an output bus and copy input to output.

### VSTGUI's `attached()/removed()` Return `bool`

Older VSTGUI versions returned `void`. Current versions return `bool`. If you get a "return type mismatch" error, check your VSTGUI version.

### MPM's Trivial Peak at Tau=0

The NSDF is always 1.0 at tau=0 (perfect self-correlation). If you include this in your key maxima search, you'll always select a near-zero lag, giving garbage frequencies. Skip past the first positive region.

---

## 17. Where to Go from Here

Congratulations -- you've built a working VST3 guitar tuner from scratch! Here are some ideas for improvement:

- **FFT-based autocorrelation**: Replace the O(N^2) loops with O(N log N) FFT for larger analysis windows
- **Drop tuning support**: Add presets for Drop D, DADGAD, Open G, etc.
- **Stroboscopic mode**: Animate the tick marks like a Peterson strobe tuner
- **Noise gate**: Ignore signal below a configurable amplitude threshold
- **macOS support**: The code is cross-platform; add a macOS CMake preset and test with AU validation
- **Dark/light themes**: Add a theme switcher using VSTGUI's style system

The full source code for this project is available in this repository. Happy tuning!
