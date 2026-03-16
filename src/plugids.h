#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace GuitarTuner {

// Processor UID
static const Steinberg::FUID TunerProcessorUID (0x1A2B3C4D, 0x5E6F7081, 0x92A3B4C5, 0xD6E7F800);

// Controller UID
static const Steinberg::FUID TunerControllerUID (0x2B3C4D5E, 0x6F708192, 0xA3B4C5D6, 0xE7F80011);

// Parameter IDs
enum ParameterIDs : Steinberg::Vst::ParamID
{
    kTuningReference   = 1,   // 432-445 Hz, default 440
    kDetectedFrequency = 2,   // read-only output
    kCentsDeviation    = 3,   // read-only output (-50 to +50)
    kDetectedNote      = 4,   // read-only output (note name as index)
    kConfidence        = 5    // read-only output (0.0-1.0)
};

#define GuitarTunerVST3Category "Fx|Analyzer"

} // namespace GuitarTuner
