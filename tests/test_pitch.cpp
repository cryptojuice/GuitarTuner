#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "dsp/ring_buffer.h"
#include "dsp/pitch_detector.h"
#include "dsp/mpm_detector.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float kSampleRate = 44100.0f;
static const size_t kWindowSize = 2048;

static int testsRun = 0;
static int testsPassed = 0;
static int testsFailed = 0;

// Generate a sine wave at the given frequency
static void generateSine (float* buffer, size_t numSamples, float frequency, float sampleRate, float amplitude = 0.8f)
{
    for (size_t i = 0; i < numSamples; i++)
    {
        buffer[i] = amplitude * std::sin (2.0f * static_cast<float> (M_PI) * frequency * static_cast<float> (i) / sampleRate);
    }
}

// Generate silence
static void generateSilence (float* buffer, size_t numSamples)
{
    std::memset (buffer, 0, numSamples * sizeof (float));
}

// Generate a guitar-like waveform (fundamental + harmonics)
static void generateGuitarLike (float* buffer, size_t numSamples, float frequency, float sampleRate, float amplitude = 0.8f)
{
    std::memset (buffer, 0, numSamples * sizeof (float));
    // Fundamental + 4 harmonics with decreasing amplitude
    float harmonicAmplitudes[] = {1.0f, 0.5f, 0.33f, 0.25f, 0.2f};
    for (int h = 0; h < 5; h++)
    {
        float freq = frequency * static_cast<float> (h + 1);
        if (freq > sampleRate / 2.0f) break; // Don't exceed Nyquist
        for (size_t i = 0; i < numSamples; i++)
        {
            buffer[i] += amplitude * harmonicAmplitudes[h] *
                std::sin (2.0f * static_cast<float> (M_PI) * freq * static_cast<float> (i) / sampleRate);
        }
    }
    // Normalize
    float maxVal = 0.0f;
    for (size_t i = 0; i < numSamples; i++)
    {
        float absVal = std::abs (buffer[i]);
        if (absVal > maxVal) maxVal = absVal;
    }
    if (maxVal > 0.0f)
    {
        for (size_t i = 0; i < numSamples; i++)
            buffer[i] = buffer[i] / maxVal * amplitude;
    }
}

// Check if detected frequency is within tolerance (in cents)
static bool isWithinCents (float detected, float expected, float toleranceCents)
{
    if (detected <= 0.0f || expected <= 0.0f)
        return false;
    float cents = 1200.0f * std::log2 (detected / expected);
    return std::abs (cents) <= toleranceCents;
}

static void runTest (const char* name, bool passed, const char* detail = nullptr)
{
    testsRun++;
    if (passed)
    {
        testsPassed++;
        std::printf ("  PASS: %s", name);
    }
    else
    {
        testsFailed++;
        std::printf ("  FAIL: %s", name);
    }
    if (detail)
        std::printf (" (%s)", detail);
    std::printf ("\n");
}

// ========================================================================
// Test Suite
// ========================================================================

static void testNoteInfo ()
{
    std::printf ("\n--- frequencyToNoteInfo tests ---\n");

    // A4 = 440 Hz
    {
        auto info = GuitarTuner::frequencyToNoteInfo (440.0f, 440.0f);
        char detail[64];
        std::snprintf (detail, sizeof (detail), "midi=%d, note=%s, cents=%.2f", info.midiNote, info.noteName, info.cents);
        runTest ("A4 at 440Hz", info.midiNote == 69 && std::strcmp (info.noteName, "A4") == 0 && std::abs (info.cents) < 0.5f, detail);
    }

    // E2 = 82.41 Hz
    {
        auto info = GuitarTuner::frequencyToNoteInfo (82.41f, 440.0f);
        char detail[64];
        std::snprintf (detail, sizeof (detail), "midi=%d, note=%s, cents=%.2f", info.midiNote, info.noteName, info.cents);
        runTest ("E2 at 82.41Hz", info.midiNote == 40 && std::strcmp (info.noteName, "E2") == 0, detail);
    }

    // Zero frequency
    {
        auto info = GuitarTuner::frequencyToNoteInfo (0.0f, 440.0f);
        runTest ("Zero frequency", info.midiNote == -1 && info.cents == 0.0f);
    }

    // A4 at A=432 reference
    {
        auto info = GuitarTuner::frequencyToNoteInfo (432.0f, 432.0f);
        char detail[64];
        std::snprintf (detail, sizeof (detail), "midi=%d, note=%s, cents=%.2f", info.midiNote, info.noteName, info.cents);
        runTest ("A4 at 432Hz ref", info.midiNote == 69 && std::abs (info.cents) < 0.5f, detail);
    }
}

static void testDetector (const char* name, GuitarTuner::PitchDetector& detector, float toleranceCents)
{
    std::printf ("\n--- %s tests (tolerance: %.0f cents) ---\n", name, toleranceCents);

    std::vector<float> buffer (kWindowSize);

    // Test standard guitar tuning frequencies (pure sine)
    struct TestNote {
        float frequency;
        const char* name;
    };

    TestNote guitarNotes[] = {
        {82.41f,   "E2 (low E)"},
        {110.00f,  "A2"},
        {146.83f,  "D3"},
        {196.00f,  "G3"},
        {246.94f,  "B3"},
        {329.63f,  "E4 (high E)"},
        {440.00f,  "A4"},
        {523.25f,  "C5"},
        {659.25f,  "E5"},
        {880.00f,  "A5"},
        {1174.66f, "D6"},
    };

    // Pure sine tests
    std::printf ("  [Pure sine waves]\n");
    for (const auto& note : guitarNotes)
    {
        generateSine (buffer.data (), kWindowSize, note.frequency, kSampleRate);
        auto result = detector.detect (buffer.data (), kWindowSize, kSampleRate);

        char detail[128];
        std::snprintf (detail, sizeof (detail), "expected=%.2fHz, got=%.2fHz, conf=%.3f",
                       note.frequency, result.frequency, result.confidence);

        bool passed = isWithinCents (result.frequency, note.frequency, toleranceCents);
        char testName[64];
        std::snprintf (testName, sizeof (testName), "Sine %s (%.0fHz)", note.name, note.frequency);
        runTest (testName, passed, detail);
    }

    // Guitar-like waveform tests (harmonics)
    std::printf ("  [Guitar-like waveforms]\n");
    TestNote guitarStrings[] = {
        {82.41f,  "E2 guitar"},
        {110.00f, "A2 guitar"},
        {146.83f, "D3 guitar"},
        {196.00f, "G3 guitar"},
        {246.94f, "B3 guitar"},
        {329.63f, "E4 guitar"},
    };

    for (const auto& note : guitarStrings)
    {
        generateGuitarLike (buffer.data (), kWindowSize, note.frequency, kSampleRate);
        auto result = detector.detect (buffer.data (), kWindowSize, kSampleRate);

        char detail[128];
        std::snprintf (detail, sizeof (detail), "expected=%.2fHz, got=%.2fHz, conf=%.3f",
                       note.frequency, result.frequency, result.confidence);

        bool passed = isWithinCents (result.frequency, note.frequency, toleranceCents);
        char testName[64];
        std::snprintf (testName, sizeof (testName), "Guitar %s", note.name);
        runTest (testName, passed, detail);
    }

    // Silence test
    {
        generateSilence (buffer.data (), kWindowSize);
        auto result = detector.detect (buffer.data (), kWindowSize, kSampleRate);
        char detail[64];
        std::snprintf (detail, sizeof (detail), "freq=%.2f, conf=%.3f", result.frequency, result.confidence);
        runTest ("Silence", result.frequency == 0.0f || result.confidence < 0.1f, detail);
    }

    // Low amplitude test (should still detect)
    {
        generateSine (buffer.data (), kWindowSize, 440.0f, kSampleRate, 0.05f);
        auto result = detector.detect (buffer.data (), kWindowSize, kSampleRate);
        char detail[64];
        std::snprintf (detail, sizeof (detail), "freq=%.2f, conf=%.3f", result.frequency, result.confidence);
        bool passed = isWithinCents (result.frequency, 440.0f, toleranceCents * 2.0f);
        runTest ("Low amplitude A4", passed, detail);
    }
}

static void testRingBuffer ()
{
    std::printf ("\n--- RingBuffer tests ---\n");

    GuitarTuner::RingBuffer<float> rb (1024);

    // Push and read
    {
        float data[100];
        for (int i = 0; i < 100; i++) data[i] = static_cast<float> (i);
        rb.push (data, 100);

        float out[50];
        rb.readLatest (out, 50);
        bool correct = true;
        for (int i = 0; i < 50; i++)
        {
            if (out[i] != static_cast<float> (50 + i))
                correct = false;
        }
        runTest ("Push and readLatest", correct);
    }

    // Clear
    {
        rb.clear ();
        runTest ("Clear resets available", rb.available () == 0);
    }

    // Wrap around
    {
        rb.clear ();
        float data[512];
        for (int i = 0; i < 512; i++) data[i] = static_cast<float> (i);
        rb.push (data, 512);
        rb.push (data, 512);
        rb.push (data, 512); // This should wrap around the 1024 buffer

        float out[256];
        rb.readLatest (out, 256);
        bool correct = true;
        for (int i = 0; i < 256; i++)
        {
            if (out[i] != static_cast<float> (256 + i))
                correct = false;
        }
        runTest ("Wrap-around readLatest", correct);
    }
}

int main ()
{
    std::printf ("=== Guitar Tuner Pitch Detection Tests ===\n");

    testNoteInfo ();
    testRingBuffer ();

    // MPM detector tests
    GuitarTuner::MpmDetector mpmDetector (0.93f);
    testDetector ("MPM", mpmDetector, 5.0f);

    std::printf ("\n=== Results: %d/%d passed, %d failed ===\n", testsPassed, testsRun, testsFailed);

    return testsFailed > 0 ? 1 : 0;
}
