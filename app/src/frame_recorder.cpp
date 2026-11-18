#include "frame_recorder.hpp"

#include <lucent/log.h>

#include <format>
#include <utility>

namespace z3d::app {

FrameRecorder::FrameRecorder(std::filesystem::path folder)
    : folder_(std::move(folder)), writer_([this](const std::stop_token &stop) {
        write(stop);
      }) {
}

FrameRecorder::~FrameRecorder() {
  writer_.request_stop();
  queued_.notify_all();
}

void FrameRecorder::add(Frame frame) {
  {
    const std::scoped_lock lock(mutex_);
    frames_.push_back(std::move(frame));
  }
  queued_.notify_one();
}

void FrameRecorder::write(const std::stop_token &stop) {
  while (true) {
    Frame frame;
    {
      std::unique_lock lock(mutex_);
      queued_.wait(lock, stop, [this] {
        return !frames_.empty();
      });
      if (frames_.empty()) {
        return;
      }
      frame = std::move(frames_.front());
      frames_.pop_front();
    }
    const std::filesystem::path path = folder_ / std::format("{:06}.png", written_++);
    if (!frame.writePng(path)) {
      lucent::warn("z3d", "cannot write {}", path.string());
    }
  }
}

} // namespace z3d::app
