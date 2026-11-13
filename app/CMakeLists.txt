cmake_minimum_required(VERSION 3.24)
project(z3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# libretro.h (MIT) from libretro-common at a pinned commit, checked by SHA-256.
set(Z3D_LIBRETRO_COMMIT "a3194c18ba65ef89734e1898a2f61403c255d368")
FetchContent_Declare(libretro_header
  URL "https://raw.githubusercontent.com/libretro/libretro-common/${Z3D_LIBRETRO_COMMIT}/include/libretro.h"
  URL_HASH "SHA256=036ddffc995ad116c3081ca0a7515d09c2a116f3483e4f5012fe0c29a885da76"
  DOWNLOAD_NO_EXTRACT TRUE
  SOURCE_SUBDIR "no-cmake")
FetchContent_MakeAvailable(libretro_header)

set(LUCENT_ROOT "$ENV{HOME}/repo/lucent" CACHE PATH "Shared Lucent checkout")
add_subdirectory("${LUCENT_ROOT}" "${CMAKE_BINARY_DIR}/lucent" EXCLUDE_FROM_ALL)
find_package(ZLIB REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
# The fork's frontend-private extensions (azahar_debug.h).
set(Z3D_AZAHAR_SOURCE "$ENV{HOME}/repo/3ds/azahar" CACHE PATH "SomeoneIsWorking/azahar checkout")

add_executable(z3d
  src/main.cpp
  src/libretro_core.cpp
  src/guest_memory.cpp
  src/frame.cpp
  src/gl_context.cpp
  src/repl.cpp)
target_include_directories(z3d SYSTEM PRIVATE "${libretro_header_SOURCE_DIR}"
  "${Z3D_AZAHAR_SOURCE}/src/citra_libretro")
target_link_libraries(z3d PRIVATE lucent ZLIB::ZLIB OpenGL::OpenGL OpenGL::EGL ${CMAKE_DL_LIBS})
target_compile_options(z3d PRIVATE -Wall -Wextra -Werror)
