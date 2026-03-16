#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace GuitarTuner {

struct NoteInfo
{
    int midiNote;       // MIDI note number (0-127)
    float cents;        // deviation from nearest note (-50 to +50)
    char noteName[8];   // e.g. "A4", "E2", "F#3"
};

struct PitchResult
{
    float frequency;    // Hz, 0 if no pitch detected
    float confidence;   // 0.0-1.0
};

// Convert a frequency to note name, MIDI note number, and cents deviation.
// refA4: reference frequency for A4 (default 440 Hz)
inline NoteInfo frequencyToNoteInfo (float frequency, float refA4 = 440.0f)
{
    NoteInfo info {};
    if (frequency <= 0.0f)
    {
        info.midiNote = -1;
        info.cents = 0.0f;
        info.noteName[0] = '-';
        info.noteName[1] = '\0';
        return info;
    }

    // MIDI note number (A4 = 69)
    float midiFloat = 69.0f + 12.0f * std::log2 (frequency / refA4);
    int midiNote = static_cast<int> (std::round (midiFloat));

    // Clamp to valid MIDI range
    if (midiNote < 0) midiNote = 0;
    if (midiNote > 127) midiNote = 127;

    // Cents deviation from nearest note
    float exactNote = 69.0f + 12.0f * std::log2 (frequency / refA4);
    float cents = (exactNote - static_cast<float> (midiNote)) * 100.0f;

    info.midiNote = midiNote;
    info.cents = cents;

    // Note name lookup
    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int noteIndex = midiNote % 12;
    int octave = (midiNote / 12) - 1;

    std::snprintf (info.noteName, sizeof (info.noteName), "%s%d", noteNames[noteIndex], octave);

    return info;
}

// Abstract base class for pitch detectors
class PitchDetector
{
public:
    virtual ~PitchDetector () = default;

    // Detect pitch from audio buffer.
    // Returns PitchResult with frequency=0 if no pitch detected.
    virtual PitchResult detect (const float* audio, size_t numSamples, float sampleRate) = 0;
};

} // namespace GuitarTuner
