#pragma once

#include "pitch_detector.h"
#include <vector>

namespace GuitarTuner {

// McLeod Pitch Method (MPM) pitch detection algorithm.
// Based on the Normalized Square Difference Function (NSDF).
class MpmDetector : public PitchDetector
{
public:
    explicit MpmDetector (float threshold = 0.93f);

    PitchResult detect (const float* audio, size_t numSamples, float sampleRate) override;

    void setThreshold (float threshold) { threshold_ = threshold; }
    float getThreshold () const { return threshold_; }

private:
    // Compute the Normalized Square Difference Function
    void computeNSDF (const float* audio, size_t numSamples);

    // Find key maxima (positive peaks between zero crossings)
    struct KeyMaximum
    {
        size_t index;
        float value;
    };
    std::vector<KeyMaximum> findKeyMaxima () const;

    // Parabolic interpolation around a peak
    float parabolicInterpolation (size_t index) const;

    float threshold_;
    std::vector<float> nsdf_; // Normalized Square Difference Function
};

} // namespace GuitarTuner
