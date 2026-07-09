# =============================================================================
#  CCRMA CLAP Workshop - self-contained plugin template
# =============================================================================
#  This ONE file builds a CLAP plugin from a single plugin.cpp. The only files
#  you edit are:
#      CMakeLists.txt   (this file)
#      plugin.cpp       (your DSP + CLAP boilerplate)
#
#  Dependencies (the CLAP SDK + clap-helpers) are downloaded from GitHub the
#  first time you configure, so you need an internet connection once.
#
#  Quick start:
#      cmake -B build                          # configure
#      cmake --build build                     # build -> build/clap-plugin-template.clap
#      cmake -B build -DCOPY_AFTER_BUILD=ON    # ...and auto-install on every build
# =============================================================================

# 3.17+ is required for `cmake -E rm -rf` (used by the auto-install step below);
# 3.21 matches the newest example in this repo (gui_example).
cmake_minimum_required(VERSION 3.21)

# -----------------------------------------------------------------------------
#  Compiler / platform settings  (MUST be set BEFORE project(), because project()
#  reads them while detecting/configuring the compiler)
# -----------------------------------------------------------------------------
# On Windows/MSVC, statically link the C++ runtime so there's no DLL to ship.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Oldest macOS this plugin runs on. 10.13 is a safe modern floor (recent Xcode
# toolchains warn below it); dependencies are header-only so there's no ABI risk.
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "Minimum macOS version")

# Plugins are dlopen'd into a host process, so all code must be position-independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_EXTENSIONS OFF)      # standard C++, no compiler-specific extensions
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # fail loudly if C++20 is unavailable

# C is enabled alongside C++ because the CLAP SDK is a C project.
project(clap-plugin-template LANGUAGES C CXX)

# -----------------------------------------------------------------------------
#  Options
# -----------------------------------------------------------------------------
# OFF by default. Turn on with:  cmake -B build -DCOPY_AFTER_BUILD=ON
# When ON, each successful build clears any previously-installed copy of this
# plugin and installs the fresh one into your user CLAP folder (see the bottom).
option(COPY_AFTER_BUILD "Install the built .clap into your user CLAP folder after each build" OFF)

# >>> EDIT ME <<< when you fork this template for your own plugin, change this
# prefix and the project() name above. The plugin's unique CLAP id becomes
#     <prefix>.<project name>      e.g.  org.ccrma.clap-plugin-template
# plugin.cpp reads it via the BUNDLE_ID compile definition set further down.
set(BUNDLE_ID_PREFIX "org.ccrma")

# -----------------------------------------------------------------------------
#  Dependencies via CPM (a thin, caching wrapper over CMake's FetchContent).
#  The block below downloads CPM.cmake itself (once) into the build folder, then
#  CPMAddPackage pulls the CLAP SDK + clap-helpers from GitHub.
#
#  Tip: set the CPM_SOURCE_CACHE env var (e.g. ~/.cache/cpm) to share downloads
#  across projects instead of re-cloning into every build folder.
#
#  ORDER MATTERS: clap-helpers fatal-errors if the `clap` target doesn't exist
#  yet, so clap must be added first.
# -----------------------------------------------------------------------------
set(CPM_DOWNLOAD_VERSION 0.42.3)
set(CPM_HASH_SUM "a609e875fd532b067174250f6abbc3dac22fe2d64869783fb1e80bda1625c844")
if(CPM_SOURCE_CACHE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()
# Expand ~ / relative paths so file(DOWNLOAD) gets an absolute location.
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)
file(DOWNLOAD
     https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
     ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM})
include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage(NAME clap         GITHUB_REPOSITORY free-audio/clap         GIT_TAG 1.2.8)
CPMAddPackage(NAME clap-helpers GITHUB_REPOSITORY free-audio/clap-helpers GIT_TAG a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc)

# -----------------------------------------------------------------------------
#  The plugin target: a MODULE library (a shared object dlopen'd by a host,
#  never linked against at build time).
# -----------------------------------------------------------------------------
add_library(${PROJECT_NAME} MODULE plugin.cpp)
# clap-core: header-only CLAP SDK (alias target).  clap-helpers: the C++ wrappers.
target_link_libraries(${PROJECT_NAME} PRIVATE clap-core clap-helpers)

# The unique id, handed to plugin.cpp as a quoted string:  .id = BUNDLE_ID
set(BUNDLE_ID "${BUNDLE_ID_PREFIX}.${PROJECT_NAME}")
target_compile_definitions(${PROJECT_NAME} PRIVATE BUNDLE_ID="${BUNDLE_ID}")

# -----------------------------------------------------------------------------
#  Per-platform packaging: turn the raw module into a loadable ".clap"
# -----------------------------------------------------------------------------
if(APPLE)
    # macOS: a ".clap" is a loadable *bundle* (a directory) and needs an
    # Info.plist. To keep this template to two files we GENERATE a minimal one
    # into the build dir. (CMake's built-in default plist hardcodes
    # CFBundlePackageType = APPL; a plugin bundle should be BNDL.)
    #
    # [=[ ... ]=] is a bracket argument: everything inside is literal, so the XML
    # (with its quotes) needs no escaping. @PROJECT_NAME@/@BUNDLE_ID@ are filled
    # in by string(CONFIGURE @ONLY).
    set(PLIST_TEMPLATE [=[
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>      <string>English</string>
    <key>CFBundleExecutable</key>             <string>@PROJECT_NAME@</string>
    <key>CFBundleIdentifier</key>             <string>@BUNDLE_ID@</string>
    <key>CFBundleInfoDictionaryVersion</key>  <string>6.0</string>
    <key>CFBundleName</key>                   <string>@PROJECT_NAME@</string>
    <key>CFBundlePackageType</key>            <string>BNDL</string>
    <key>CFBundleShortVersionString</key>     <string>1.0</string>
    <key>CFBundleVersion</key>                <string>1</string>
    <key>CFBundleSignature</key>              <string>????</string>
</dict>
</plist>
]=])
    string(CONFIGURE "${PLIST_TEMPLATE}" PLIST_RESOLVED @ONLY)
    set(GENERATED_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    file(WRITE "${GENERATED_PLIST}" "${PLIST_RESOLVED}")

    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUNDLE                   TRUE
        BUNDLE_EXTENSION         clap
        MACOSX_BUNDLE_INFO_PLIST "${GENERATED_PLIST}")
else()
    # Linux (.so) / Windows (.dll): a ".clap" is a single file named "<name>.clap".
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".clap" PREFIX "")
endif()

# -----------------------------------------------------------------------------
#  Optional: clear + auto-install into the user CLAP folder after each build.
#
#  Cross-platform subtlety:
#    * macOS build product is a BUNDLE DIRECTORY -> copy_directory
#    * Linux/Windows build product is a SINGLE FILE -> copy
#  Either way we first `rm -rf` the stale install (works on a dir OR a file) so
#  no old files linger inside the bundle.
# -----------------------------------------------------------------------------
if(COPY_AFTER_BUILD)
    # 1. Where does this OS look for user CLAP plugins? (user-writable, no admin)
    if(APPLE)
        set(CLAP_USER_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    elseif(UNIX)
        set(CLAP_USER_DIR "$ENV{HOME}/.clap")
    else() # Windows
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/Common/CLAP" CLAP_USER_DIR)
    endif()
    set(INSTALLED_CLAP "${CLAP_USER_DIR}/${PROJECT_NAME}.clap")

    # 2. What is the build product, and how do we copy it?
    if(APPLE)
        set(BUILT_CLAP "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>") # the .clap directory
        set(INSTALL_CMD copy_directory)
    else()
        set(BUILT_CLAP "$<TARGET_FILE:${PROJECT_NAME}>")       # the .clap file
        set(INSTALL_CMD copy)
    endif()

    message(STATUS "COPY_AFTER_BUILD is ON -> installing to ${CLAP_USER_DIR}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${PROJECT_NAME}.clap -> ${CLAP_USER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_USER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf         "${INSTALLED_CLAP}"
        COMMAND ${CMAKE_COMMAND} -E ${INSTALL_CMD} "${BUILT_CLAP}" "${INSTALLED_CLAP}"
        VERBATIM)
endif()
