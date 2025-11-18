# GScript.RemoteControl

A visual clean-room recreation of Graal RemoteControl 3, rebuilt in C++20 with GTK3. It targets Windows, Linux, and macOS while preserving observed RC3 behavior and its GRClib protocol integration.

The original RemoteControl was created by Stefan Knorr. This project is independent and unaffiliated.

## Dependency

RC3 builds against [GRClib](https://github.com/MorenoLand/GScript.GRClib). Clone it beside this repository, or provide its location with `GRCLIB_SOURCE_DIR`.

```sh
git clone https://github.com/MorenoLand/GScript.RemoteControl.git RC3
git clone https://github.com/MorenoLand/GScript.GRClib.git grclib
cmake -S RC3 -B RC3/build -DGRCLIB_SOURCE_DIR=../grclib
cmake --build RC3/build
ctest --test-dir RC3/build --output-on-failure
```

The runtime assets and Windows DLLs required by the checked-in build layout live in `bin/`. Executables are deliberately ignored and must be built locally or obtained from CI artifacts.

## License

The reconstructed source code is MIT licensed. Original Graal artwork, logos, and other supplied runtime assets remain the property of their respective owners.
