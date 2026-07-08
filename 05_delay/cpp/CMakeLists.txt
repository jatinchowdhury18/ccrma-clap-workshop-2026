cmake_minimum_required(VERSION 3.15)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.11 CACHE STRING "Build for 10.1")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../helpers")

project(delay-plugin LANGUAGES C CXX)

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

add_library(${PROJECT_NAME} MODULE plugin.cpp)
target_link_libraries(${PROJECT_NAME} clap-core clap-helpers)

include(package_clap)
package_clap(${PROJECT_NAME})
