# Build instructions

First, clone the `clap-jai` module somewhere on your system:

```bash
git clone https://github.com/jatinchowdhury18/clap-jai ../../helpers/clap-jai
```

Then build the plugin, pointing the compiler at that modules directory:

```bash
jai -import_dir ../../helpers build_clap.jai
```

For an optimized release build, pass `-release` after the source file:

```bash
jai -import_dir ../../helpers build_clap.jai - -release
```

The build will produce:
- **macOS** – `dynamics_plugin.clap/` (a `.clap` bundle directory)
- **Windows** – `dynamics_plugin.clap`
- **Linux** – `dynamics_plugin.clap`

Copy the result to your system's CLAP plugin folder and load it in your DAW.
