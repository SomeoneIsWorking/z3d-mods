// The window's presents saved as numbered PNGs (--record) on a thread of their own, so that
// encoding them does not hold up the frames being recorded.
#pragma once

#include "frame.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace z3d::app {

class FrameRecorder {
public:
  explicit FrameRecorder(std::filesystem::path folder);
  ~FrameRecorder();
  FrameRecorder(const FrameRecorder &) = delete;
  FrameRecorder &operator=(const FrameRecorder &) = delete;

  // Queues the next present; it is written as <folder>/<index>.png, numbered from 0.
  void add(Frame frame);

private:
  void write(const std::stop_token &stop);

  std::filesystem::path folder_;
  std::mutex mutex_;
  std::condition_variable_any queued_;
  std::deque<Frame> frames_;
  uint64_t written_ = 0;
  std::jthread writer_;
};

} // namespace z3d::app
