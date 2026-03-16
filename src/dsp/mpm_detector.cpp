#include "mpm_detector.h"
#include <cmath>
#include <algorithm>

namespace GuitarTuner {

MpmDetector::MpmDetector (float threshold)
    : threshold_ (threshold)
{
}

PitchResult MpmDetector::detect (const float* audio, size_t numSamples, float sampleRate)
{
    PitchResult result {};
    result.frequency = 0.0f;
    result.confidence = 0.0f;

    if (numSamples < 2)
        return result;

    // Step 1: Compute NSDF
    computeNSDF (audio, numSamples);

    // Step 2: Find key maxima
    std::vector<KeyMaximum> keyMaxima = findKeyMaxima ();
    if (keyMaxima.empty ())
        return result;

    // Step 3: Select the highest key maximum that exceeds the threshold
    // relative to the absolute highest peak
    float highestPeak = 0.0f;
    for (const auto& km : keyMaxima)
    {
        if (km.value > highestPeak)
            highestPeak = km.value;
    }

    float cutoff = highestPeak * threshold_;

    // Find the first key maximum that exceeds the cutoff
    size_t selectedIndex = 0;
    float selectedValue = -1.0f;
    bool found = false;

    for (const auto& km : keyMaxima)
    {
        if (km.value >= cutoff)
        {
            selectedIndex = km.index;
            selectedValue = km.value;
            found = true;
            break;
        }
    }

    if (!found)
        return result;

    // Step 4: Parabolic interpolation
    float refinedTau = parabolicInterpolation (selectedIndex);

    if (refinedTau <= 0.0f)
        return result;

    result.frequency = sampleRate / refinedTau;
    result.confidence = selectedValue;

    // Clamp confidence
    if (result.confidence < 0.0f) result.confidence = 0.0f;
    if (result.confidence > 1.0f) result.confidence = 1.0f;

    return result;
}

void MpmDetector::computeNSDF (const float* audio, size_t numSamples)
{
    // Only compute up to half the window to avoid noisy tail values
    // where few overlapping samples make the NSDF unreliable
    size_t maxTau = numSamples / 2;
    nsdf_.resize (maxTau);

    for (size_t tau = 0; tau < maxTau; tau++)
    {
        float acf = 0.0f;     // Autocorrelation r(tau)
        float energy1 = 0.0f; // sum of x[j]^2 for j=0..W-1-tau
        float energy2 = 0.0f; // sum of x[j]^2 for j=tau..W-1

        size_t count = numSamples - tau;
        for (size_t j = 0; j < count; j++)
        {
            acf += audio[j] * audio[j + tau];
            energy1 += audio[j] * audio[j];
            energy2 += audio[j + tau] * audio[j + tau];
        }

        float denom = energy1 + energy2;
        if (denom > 0.0f)
            nsdf_[tau] = 2.0f * acf / denom;
        else
            nsdf_[tau] = 0.0f;
    }
}

std::vector<MpmDetector::KeyMaximum> MpmDetector::findKeyMaxima () const
{
    std::vector<KeyMaximum> maxima;

    if (nsdf_.size () < 3)
        return maxima;

    // Skip the initial positive region connected to the trivial peak at tau=0.
    // The first meaningful key maximum is in the second positive region,
    // after the NSDF has gone negative for the first time.
    size_t startIdx = 1;
    while (startIdx < nsdf_.size () - 1 && nsdf_[startIdx] > 0.0f)
        startIdx++;

    // Find positive regions and their maxima
    bool wasPositive = false;
    float currentMax = 0.0f;
    size_t currentMaxIndex = 0;

    for (size_t i = startIdx; i < nsdf_.size () - 1; i++)
    {
        if (nsdf_[i] > 0.0f)
        {
            if (!wasPositive)
            {
                // Entering positive region
                wasPositive = true;
                currentMax = nsdf_[i];
                currentMaxIndex = i;
            }
            else if (nsdf_[i] > currentMax)
            {
                currentMax = nsdf_[i];
                currentMaxIndex = i;
            }
        }
        else if (wasPositive)
        {
            // Leaving positive region - record the maximum
            maxima.push_back ({currentMaxIndex, currentMax});
            wasPositive = false;
            currentMax = 0.0f;
        }
    }

    // Handle case where we end in a positive region
    if (wasPositive && currentMax > 0.0f)
    {
        maxima.push_back ({currentMaxIndex, currentMax});
    }

    return maxima;
}

float MpmDetector::parabolicInterpolation (size_t index) const
{
    if (index == 0 || index >= nsdf_.size () - 1)
        return static_cast<float> (index);

    float s0 = nsdf_[index - 1];
    float s1 = nsdf_[index];
    float s2 = nsdf_[index + 1];

    float denom = 2.0f * s1 - s2 - s0;
    if (std::abs (denom) < 1e-12f)
        return static_cast<float> (index);

    float adjustment = (s2 - s0) / (2.0f * denom);

    // Clamp adjustment
    if (adjustment > 1.0f) adjustment = 1.0f;
    if (adjustment < -1.0f) adjustment = -1.0f;

    return static_cast<float> (index) + adjustment;
}

} // namespace GuitarTuner
