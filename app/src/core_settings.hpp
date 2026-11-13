// The core's settings and user path from the app's config; applied before the window and the
// game exist, since both read them.
#pragma once

#include "app_config.hpp"

namespace z3d::app {

void applyCoreSettings(const AppConfig &config);

} // namespace z3d::app
