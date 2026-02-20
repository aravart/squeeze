#pragma once

#include "core/Buffer.h"

#include <cmath>

namespace squeeze {

enum class LoopMode { off, forward, pingPong };

class PlaybackCursor {
public:
    PlaybackCursor();
    ~PlaybackCursor();

    // Non-copyable, movable
    PlaybackCursor(const PlaybackCursor&) = delete;
    PlaybackCursor& operator=(const PlaybackCursor&) = delete;
    PlaybackCursor(PlaybackCursor&&) noexcept;
    PlaybackCursor& operator=(PlaybackCursor&&) noexcept;

    // --- Configuration (control thread or before render) ---
    void prepare(double engineSampleRate);
    void reset();

    // --- Render (audio thread, RT-safe) ---

    /// Render numSamples into destL/destR from the buffer at the current position.
    int render(const Buffer* buffer, float* destL, float* destR,
               int numSamples, double rate,
               LoopMode loopMode, double loopStart, double loopEnd,
               double fadeSamples);

    // --- Position (lock-free) ---

    void seek(double normalizedPosition, const Buffer* buffer, double fadeSamples);
    double getPosition(const Buffer* buffer) const;
    double getRawPosition() const;
    void setRawPosition(double samplePosition);
    bool isStopped() const;

private:
    double position_ = 0.0;
    double engineSampleRate_ = 44100.0;
    bool stopped_ = false;
    int direction_ = 1;

    // Crossfade state for seeks
    bool crossfading_ = false;
    double crossfadePosition_ = 0.0;
    double crossfadeRemaining_ = 0.0;
    double crossfadeLength_ = 0.0;

    float interpolate(const float* data, int length, double pos) const;
};

} // namespace squeeze
