# Built as an out-of-tree frontend of the SomeoneIsWorking/azahar fork:
#   cmake -S <azahar> -B build/app -DAZAHAR_EXTRA_FRONTENDS=<this dir> ...  (tools/z3dmods/app.py)
if(NOT TARGET citra_core)
  message(FATAL_ERROR "app/ builds inside the Azahar tree: configure Azahar with -DAZAHAR_EXTRA_FRONTENDS=${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(LUCENT_ROOT "$ENV{HOME}/repo/lucent" CACHE PATH "Shared Lucent checkout")
add_subdirectory("${LUCENT_ROOT}" "${CMAKE_BINARY_DIR}/lucent" EXCLUDE_FROM_ALL)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3-shared)

add_executable(z3d
  src/main.cpp
  src/app_config.cpp
  src/game_window.cpp
  src/pad_input.cpp
  src/emulator.cpp
  src/frame.cpp
  src/repl.cpp
  src/player.cpp)
target_link_libraries(z3d PRIVATE citra_common citra_core video_core audio_core lucent SDL3::SDL3)
target_compile_options(z3d PRIVATE -Wall -Wextra -Werror)
set_target_properties(z3d PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
