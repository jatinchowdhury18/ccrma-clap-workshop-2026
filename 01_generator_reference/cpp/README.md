# Build instructions

```bash
cmake -Bbuild -G<generator>
cmake --build build --target generator-plugin --parallel --config <Debug|Release>
cp -r build/<Debug|Release>/generator-plugin.clap path/to/plugin/folder
```
