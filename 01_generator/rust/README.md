# Build instructions

```bash
cargo xtask bundle
```

For a release build:

```bash
cargo xtask bundle --release
```

To bundle and install directly to the system CLAP plugin directory:

```bash
cargo xtask install --release
```
