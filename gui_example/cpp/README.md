# About

This is a simple CLAP template that uses Visage (https://github.com/vitalaudio/visage) as its graphics/UI backend. 

# Build instructions

```bash
cmake -Bbuild -G<generator>
cmake --build build --target gui-example-plugin --parallel --config <Debug|Release>
rm -rf path/to/plugin/folder/gui-example-plugin.clap
cp -r build/<Debug|Release>/gui-example-plugin.clap path/to/plugin/folder
```

The first  CMAKE configure downloads and builds Visage's rendering stack (bgfx,
freetype), which requires a network connection and may take a few minutes.