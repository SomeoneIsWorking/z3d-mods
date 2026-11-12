#include "gl_context.hpp"

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include <lucent/log.h>

#include <stdexcept>
#include <string>

namespace z3d::harness {
namespace {

void check(bool ok, const char *what) {
  if (!ok) {
    throw std::runtime_error(std::string(what) + " failed, EGL error 0x" +
                             std::to_string(eglGetError()));
  }
}

} // namespace

GlContext::GlContext(const Request &request) {
  const auto [major, minor, size, depth, stencil] = request;
  const auto [width, height] = size;
  display_ = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  check(display_ != EGL_NO_DISPLAY, "eglGetPlatformDisplay(surfaceless)");
  check(eglInitialize(display_, nullptr, nullptr) == EGL_TRUE, "eglInitialize");
  check(eglBindAPI(EGL_OPENGL_API) == EGL_TRUE, "eglBindAPI");
  const EGLint configAttributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, 0,
                                     EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  check(eglChooseConfig(display_, configAttributes, &config, 1, &count) == EGL_TRUE && count == 1,
        "eglChooseConfig");
  const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                      static_cast<EGLint>(major),
                                      EGL_CONTEXT_MINOR_VERSION,
                                      static_cast<EGLint>(minor),
                                      EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                      EGL_NONE};
  context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
  check(context_ != EGL_NO_CONTEXT, "eglCreateContext");
  check(eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_) == EGL_TRUE,
        "eglMakeCurrent");
  lucent::info("gl", "{} on {}", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
               reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

  glGenTextures(1, &color_);
  glBindTexture(GL_TEXTURE_2D, color_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height));
  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
  if (depth) {
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                          static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                              stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_);
  }
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    throw std::runtime_error("framebuffer incomplete");
  }
}

GlContext::~GlContext() {
  glDeleteFramebuffers(1, &framebuffer_);
  glDeleteTextures(1, &color_);
  if (depth_ != 0) {
    glDeleteRenderbuffers(1, &depth_);
  }
  eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display_, context_);
  eglTerminate(display_);
}

void *GlContext::procAddress(const char *name) {
  return reinterpret_cast<void *>(eglGetProcAddress(name));
}

void GlContext::read(FrameSize size, std::vector<uint32_t> &out) const {
  const auto [width, height] = size;
  out.resize(static_cast<size_t>(width) * height);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_BGRA,
               GL_UNSIGNED_INT_8_8_8_8_REV, out.data());
}

} // namespace z3d::harness
