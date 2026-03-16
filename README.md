# GuitarTuner VST3 Plugin

A fast, accurate chromatic guitar tuner VST3 plugin built in C++.

## Features

- **Real-time Pitch Detection**: Captures audio and detects the pitch of your guitar strings quickly and accurately.
  - **MPM Algorithm (McLeod Pitch Method)**: An algorithm that uses normalized square difference function for highly accurate detection on clean signals.
- **Smooth GUI**: Includes a custom UI built with VSTGUI, featuring a smooth, animated needle gauge to show exact tuning deviations (in cents).
- **Cross-Platform Readiness**: Code is designed to be cross-platform and compatible with major DAWs including Ableton Live, Cubase, and REAPER.

## Technical Details

- **Audio Processing**: Fast, allocation-free, and lock-free real-time audio thread implementation.
- **Ring Buffer**: A lock-free ring buffer accumulates audio data seamlessly for analysis windows without stuttering or blocking the DAW.
- **Architecture**: Employs the recommended VST3 split architecture, separating the Audio Processor from the Edit Controller to ensure optimal performance.

## Build Prerequisites

To build this plugin from source, you will need:

- **Windows 10/11** (Visual Studio 2022 or later)
- **CMake 3.19 or higher**
- **Git**
- **A modern DAW** (for testing the VST3 plugin)

## Building the Plugin

1. Clone this repository with its submodules (it uses the official VST3 SDK):
   ```bash
   git clone --recursive <repository-url> GuitarTuner
   cd GuitarTuner
   ```
   *(If you've already cloned without submodules, run `git submodule update --init --recursive`)*

2. Use CMake to configure and build:
   ```bash
   mkdir build && cd build
   cmake ..
   cmake --build . --config Release
   ```

3. Locate the `.vst3` file in the build output and copy it to your system's VST3 plugins folder (`C:\Program Files\Common Files\VST3\` on Windows).

## Learning and Tutorial

This project was built following a comprehensive step-by-step tutorial. Please see [`TUTORIAL.md`](TUTORIAL.md) for a deep dive into the audio architecture, algorithm mathematics, and VST3 APIs.
