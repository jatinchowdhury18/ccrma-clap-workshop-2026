# Build instructions

```bash
cmake -Bbuild -G<generator>
cmake --build build --target filter-plugin --parallel --config <Debug|Release>
cp -r build/<Debug|Release>/filter-plugin.clap path/to/plugin/folder
```
