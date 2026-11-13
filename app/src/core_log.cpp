#include "core_log.hpp"

#include "common/logging/backend.h"

namespace z3d::app {

CoreLog::CoreLog() {
  Common::Log::Initialize("z3d_core.log");
  Common::Log::Start();
}

CoreLog::~CoreLog() {
  Common::Log::Stop();
}

} // namespace z3d::app
