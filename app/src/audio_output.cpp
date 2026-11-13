#include "audio_output.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace z3d::app {

AudioOutput::AudioOutput(double sampleRate) : bytesPerSecond_(sampleRate * 4) {
  const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(std::lround(sampleRate))};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (stream_ == nullptr) {
    throw std::runtime_error(std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError());
  }
  SDL_ResumeAudioStreamDevice(stream_);
}

AudioOutput::~AudioOutput() {
  SDL_DestroyAudioStream(stream_);
}

void AudioOutput::push(const int16_t *samples, size_t frames) {
  SDL_PutAudioStreamData(stream_, samples, static_cast<int>(frames * 4));
}

double AudioOutput::queued() const {
  return static_cast<double>(SDL_GetAudioStreamQueued(stream_)) / bytesPerSecond_;
}

} // namespace z3d::app
