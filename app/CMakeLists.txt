cmake_minimum_required(VERSION 3.24)
project(z3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The SomeoneIsWorking/azahar core, installed once per revision (tools/z3dmods/app.py).
find_package(AzaharCore CONFIG REQUIRED)
set(LUCENT_ROOT "$ENV{HOME}/repo/lucent" CACHE PATH "Shared Lucent checkout")
add_subdirectory("${LUCENT_ROOT}" "${CMAKE_BINARY_DIR}/lucent" EXCLUDE_FROM_ALL)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3-shared)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
# The shared first-run screen (RmlUi from the checkout beside it), after SDL3.
set(SETUP_UI_ROOT "$ENV{HOME}/repo/shared/setup-ui" CACHE PATH "Shared setup-ui checkout")
add_subdirectory("${SETUP_UI_ROOT}" "${CMAKE_BINARY_DIR}/setup-ui" EXCLUDE_FROM_ALL)

add_executable(z3d
  src/main.cpp
  src/app_config.cpp
  src/core_settings.cpp
  src/core_log.cpp
  src/game_library.cpp
  src/game_options.cpp
  src/options_menu.cpp
  src/title_picker.cpp
  src/menu_canvas.cpp
  src/folder_setup.cpp
  src/folder_browser.cpp
  src/tool_process.cpp
  src/set_install.cpp
  src/game_window.cpp
  src/pad_input.cpp
  src/mouse_aim.cpp
  src/emulator.cpp
  src/frame.cpp
  src/repl.cpp
  src/screen_overlay.cpp
  src/hud.cpp
  src/hud_atlas.cpp
  src/mod_swap.cpp
  src/player.cpp)
target_link_libraries(z3d PRIVATE AzaharCore::azahar_core lucent SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image setup_ui::setup_ui)
target_compile_options(z3d PRIVATE -Wall -Wextra -Werror)
