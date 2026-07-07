cmake_minimum_required(VERSION 3.15)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.11 CACHE STRING "Build for 10.1")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../helpers")

project(dynamics-plugin LANGUAGES C)

include(CPM)
CPMAddPackage(
  NAME clap
  GITHUB_REPOSITORY free-audio/clap
  GIT_TAG 1.2.8
)

add_library(${PROJECT_NAME} MODULE plugin.c)
target_link_libraries(${PROJECT_NAME} clap-core)
target_compile_options(${PROJECT_NAME} PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W4,-Wall>)

include(package_clap)
package_clap(${PROJECT_NAME})
