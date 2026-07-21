# GScript.RemoteControl

A GTK3 recreation of Remote Control, rebuilt in C++20 for Windows, Linux, and macOS with GRClib protocol integration.

The original RemoteControl was created by Stefan Knorr. This project is independent and unaffiliated.

## Dependency

Remote Control builds against [GRClib](https://github.com/MorenoLand/GScript.GRClib). Clone it beside this repository, or provide its location with `GRCLIB_SOURCE_DIR`.

```sh
git clone https://github.com/MorenoLand/GScript.RemoteControl.git RemoteControl
git clone https://github.com/MorenoLand/GScript.GRClib.git grclib
cmake -S RemoteControl -B RemoteControl/build -DGRCLIB_SOURCE_DIR=../grclib
cmake --build RemoteControl/build
ctest --test-dir RemoteControl/build --output-on-failure
```

The runtime assets and Windows DLLs required by the checked-in build layout live in `bin/`. Executables are deliberately ignored and must be built locally or obtained from CI artifacts.

## License

The reconstructed source code is MIT licensed. Original Graal artwork, logos, and other supplied runtime assets remain the property of their respective owners.
