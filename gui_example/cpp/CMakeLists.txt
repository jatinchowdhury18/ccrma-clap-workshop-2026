cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Visage's Metal/bgfx rendering stack needs a much newer macOS than the other examples.
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3 CACHE STRING "Build for macOS 13.3")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../helpers")

project(gui-example-plugin LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

include(CPM)
CPMAddPackage(
  NAME clap
  GITHUB_REPOSITORY free-audio/clap
  GIT_TAG 1.2.8
)
CPMAddPackage(
  NAME clap-helpers
  GITHUB_REPOSITORY free-audio/clap-helpers
  GIT_TAG a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc
)
CPMAddPackage(
  NAME readerwriterqueue
  GITHUB_REPOSITORY cameron314/readerwriterqueue
  GIT_TAG v1.0.7
)
CPMAddPackage(
  NAME visage
  GITHUB_REPOSITORY vitalaudio/visage
  GIT_TAG b0b2ee8 # visage pushes breaking changes now and then; keep this pinned
  OPTIONS "VISAGE_BUILD_EXAMPLES OFF" "VISAGE_BUILD_TESTS OFF"
)

add_library(${PROJECT_NAME} MODULE plugin.cpp)
target_link_libraries(${PROJECT_NAME}
    clap-core
    clap-helpers
    readerwriterqueue
    visage              # transitively links bgfx/freetype + the macOS frameworks
    VisageEmbeddedFonts # provides "embedded/fonts.h" for the slider labels
    # (the harmless "duplicate libraries" linker warning is this font archive
    # reaching the link line twice: once here, once through visage)
)

include(package_clap)
package_clap(${PROJECT_NAME})
