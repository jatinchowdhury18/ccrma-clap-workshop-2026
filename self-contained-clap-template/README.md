# Self-contained CLAP plugin template

A self-contained copy-anywhere starting point for a CLAP plugin.

## Requirements

- CMake ≥ 3.21 and a C++20 compiler (Xcode/clang, GCC, or MSVC).
- An internet connection for the first time you configure

## Build

```bash
cmake -B build            # configure (downloads deps on first run)
cmake --build build       # -> build/clap-plugin-template.clap
```

## Install it where your DAW can find it (optional)

Pass `-DCOPY_AFTER_BUILD=ON` and every build will **clear** any previously
installed copy and **copy** the fresh `.clap` into your user plugin folder:

```bash
cmake -B build -DCOPY_AFTER_BUILD=ON
cmake --build build
```

## Forking this template for your own plugin

To adapt this template to your own plugin

1. `project(clap-plugin-template ...)` → your plugin's name.
2. `set(BUNDLE_ID_PREFIX "org.ccrma")` → your own domain prefix

Together these form the plugin's unique CLAP id (`<prefix>.<project name>`), which
`plugin.cpp` reads through the `BUNDLE_ID` compile definition. 

Then, edit and adapt the `clap_plugin_descriptor_t` fields (name, vendor, description) in `plugin.cpp`.
