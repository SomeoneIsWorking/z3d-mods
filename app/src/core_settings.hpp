// The core's settings and user path from the app's config; applied before the window and the
// game exist, since both read them.
#pragma once

#include "app_config.hpp"

namespace z3d::app {

// Once, before the window.
void applyCoreSettings(const AppConfig &config);
// Before each game loads, and before a window for a direct launch takes its shape.
void applyGameSettings(const GameSetup &game);

} // namespace z3d::app
