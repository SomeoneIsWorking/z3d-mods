#pragma once

#include "frame.hpp"

#include <cstdint>
#include <vector>

using EGLDisplay = void *;
using EGLContext = void *;

namespace z3d::harness {

// A windowless OpenGL core context on the GPU (EGL surfaceless) with one framebuffer the core
// renders into.
class GlContext {
public:
  struct Request {
    unsigned major = 0;
    unsigned minor = 0;
    FrameSize size;
    bool depth = false;
    bool stencil = false;
  };

  explicit GlContext(const Request &request);
  ~GlContext();
  GlContext(const GlContext &) = delete;
  GlContext &operator=(const GlContext &) = delete;

  [[nodiscard]] unsigned framebuffer() const {
    return framebuffer_;
  }
  static void *procAddress(const char *name);

  // The bottom-left size of the framebuffer as XRGB8888, rows bottom-up.
  void read(FrameSize size, std::vector<uint32_t> &out) const;

private:
  EGLDisplay display_ = nullptr;
  EGLContext context_ = nullptr;
  unsigned framebuffer_ = 0;
  unsigned color_ = 0;
  unsigned depth_ = 0;
};

} // namespace z3d::harness
