# Build instructions

```bash
cmake -Bbuild -G<generator>
cmake --build build --target multiband-distortion-plugin --parallel --config <Debug|Release>
cp -r build/<Debug|Release>/multiband-distortion-plugin.clap path/to/plugin/folder
```
