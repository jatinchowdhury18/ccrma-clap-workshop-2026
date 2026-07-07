# Build instructions

```bash
cmake -Bbuild -G<generator>
cmake --build build --target dynamics-plugin --parallel --config <Debug|Release>
cp -r build/<Debug|Release>/dynamics-plugin.clap path/to/plugin/folder
```
