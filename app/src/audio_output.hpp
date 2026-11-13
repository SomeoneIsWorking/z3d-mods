// The core's sound on the default playback device, and the clock the frame loop is paced by.
#pragma once

#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

namespace z3d::app {

class AudioOutput {
public:
  // Stereo signed 16-bit at the core's `sampleRate`; SDL resamples to the device.
  explicit AudioOutput(double sampleRate);
  ~AudioOutput();
  AudioOutput(const AudioOutput &) = delete;
  AudioOutput &operator=(const AudioOutput &) = delete;
  AudioOutput(AudioOutput &&) = delete;
  AudioOutput &operator=(AudioOutput &&) = delete;

  // `frames` interleaved stereo frames.
  void push(const int16_t *samples, size_t frames);
  // Seconds of sound not yet played.
  [[nodiscard]] double queued() const;

private:
  SDL_AudioStream *stream_ = nullptr;
  double bytesPerSecond_;
};

} // namespace z3d::app
