# Build instructions

```bash
cmake -Bbuild -G<generator>
cmake --build build --target delay-plugin --parallel --config <Debug|Release>
cp -r build/<Debug|Release>/delay-plugin.clap path/to/plugin/folder
```
