# GScript.RemoteControl

A GTK3 recreation of Remote Control, rebuilt in C++20 for Windows, Linux, and macOS with GRClib protocol integration.

The original RemoteControl was created by Stefan Knorr. This project is independent and unaffiliated.

## RC features

- Multiple simultaneous server sessions, each with its own window, connection, chat, player state, and sign-out action.
- Server-list context actions for opening an additional session, including choosing a saved account associated with that list server.
- Tray connection management with separators, long-name sizing, and multiple primary sessions. Marked sessions are shown with `[*]` and can be opened from the tray icon.
- Portable per-user account storage with stable account identities, saved list-server associations, and authenticated encrypted password storage.
- Named list-server profiles, including the protected Retail profile, with automatic persistence and compact settings management.
- Native MCP stdio integration for local AI clients, with capability scopes, approvals, audit logging, connection selection, and RC-backed status/editor tools.
- Script editor tooling including GraalScript-aware formatting, comment removal, find/replace, minimap support, live diagnostics, autocomplete/LSP integration, and bounded MCP editor inspection/editing.
- RC chat conveniences including clickable URLs, separate find-result tabs, custom `/rchelp` command help, emote-aware help output, unread private-message indicators, and configurable fonts/themes.
- Server and file-browser presentation using server-type, player-status, and file-type icons from the bundled asset set.

## Dependency

Remote Control builds against [GRClib](https://github.com/MorenoLand/GScript.GRClib). Clone it beside this repository, or provide its location with `GRCLIB_SOURCE_DIR`.

  ```sh
  git clone https://github.com/MorenoLand/GScript.RemoteControl.git RemoteControl
  git clone https://github.com/MorenoLand/GScript.GRClib.git grclib
build.bat

`build.bat` is the supported Windows build entry point. It requires MSYS2 MINGW64 at `D:\msys64`, stops only a running `RemoteControl.exe`, configures the RC tree with the adjacent `grclib` checkout, and builds `bin\RemoteControl.exe`.

For a manual build, use the same MINGW64 toolchain:

set PATH=D:\msys64\mingw64\bin;D:\msys64\usr\bin;%PATH%
cmake -S . -B build-local-mingw-rc -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRCLIB_SOURCE_DIR=..\grclib
cmake --build build-local-mingw-rc --parallel
ctest --test-dir build-local-mingw-rc --output-on-failure
  ```

On Windows with MSYS2 installed at `D:\msys64`, run `build.bat` from the repository root. It stops only a running `RemoteControl.exe`, configures `build-local` with the adjacent GRClib checkout, and builds the release executable.

The runtime assets and Windows DLLs required by the checked-in build layout live in `bin/`. Executables are deliberately ignored and must be built locally or obtained from CI artifacts.

## License

The reconstructed source code is MIT licensed. Original artwork, logos, and other supplied runtime assets remain the property of their respective owners.
