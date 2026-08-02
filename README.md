# GScript.RemoteControl

GScript.RemoteControl is a C++20/GTK3 Remote Control client using [GRClib](https://github.com/MorenoLand/GScript.GRClib) for Graal server protocol support. It supports independent concurrent server sessions, script and server-text editing, file browsing, Git backups, and locally packaged extensions.

The original RemoteControl was created by Stefan Knorr. This project is independent and unaffiliated.

## Distinctive features

- Multiple simultaneous server sessions with saved encrypted accounts and named list-server profiles.
- GraalScript editor with formatting, diagnostics, autocomplete, find/replace, minimap, and MCP editor tools.
- Script, class, server-options, server-flags, and folder-config editing; optionally launch those text files in a configured external editor and upload saved changes.
- Offline GS2 analysis combines API/LSP signatures, live underlines, parameter checks, and case-insensitive engine lookup without pretending to be server validation.
- Modern file-browser mode uses a lazy, bounded preview cache, serialized transfers, drag-out/upload support, and server-relative sync paths without blocking the GTK loop.
- File and server synchronization tracks folder trees, configuration files, progress/ETA, protected timestamps, exclusions, direction, and explicit deletion permission.
- Manifest-based extensions with enable/disable/remove state, captured output, bounded logs, status/error details, and optional output tabs.
- Extension packages can declare publisher, runtime/API revision, automatic discovery, requested capabilities, and approved UI slots; the Extensions window exposes that metadata and manifest failures.
- Hidden local stdio runtimes use asynchronous JSON-lines transport, generic human-readable display fields, lifecycle control, and per-extension output routing without exposing RC memory or GTK widgets.
- Extension state is persisted per user, output tabs are opt-in and closable, and disabled packages are not launched on later sessions.

## Build

Clone GRClib beside this repository, or set `GRCLIB_SOURCE_DIR` to an existing checkout:

```sh
git clone https://github.com/MorenoLand/GScript.RemoteControl.git RC3
git clone https://github.com/MorenoLand/GScript.GRClib.git grclib
```

### Windows

`build.bat` is the supported Windows entry point. It defaults to MSYS2 MINGW64 at `D:\msys64`, stops only a running `RemoteControl.exe`, and stages the executable and runtime dependencies in `bin\`. Set `MSYS2_ROOT` before running it to use another installation.

For a manual build:

```powershell
$env:PATH = 'D:\msys64\mingw64\bin;D:\msys64\usr\bin;' + $env:PATH
cmake -S . -B build-local-mingw-rc -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRCLIB_SOURCE_DIR=..\grclib
cmake --build build-local-mingw-rc --parallel
ctest --test-dir build-local-mingw-rc --output-on-failure
```

Run `bin\RemoteControl.exe`.

### Linux

The core build is CMake/GTK3 based. Install development packages for GTK3, GtkSourceView 3, WebP demux, Fontconfig, OpenSSL, CMake, Ninja, pkg-config, and a C++20 compiler, then build with:

```sh
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRCLIB_SOURCE_DIR=../grclib
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
```

Linux uses a per-user Unix-domain socket for its MCP GUI bridge. Windows Explorer drag-out is Windows-specific; Linux tray and global-hotkey support depend on the active desktop session.

#### Linux runtime dependencies

Users running a staged build do not need development packages. Install the runtime libraries with:

```sh
sudo apt install libgtksourceview-3.0-1 libgtk-3-0t64 libwebp7 libwebpdemux2 libfontconfig1 libssl3t64
```

## Runtime layout

`bin/` is the staged runtime directory. Build output and generated build directories are intentionally untracked. Extension packages belong under `bin/extensions/` and are discovered by their manifests.

## Extensions

An extension is a directory directly under `bin/extensions/`. Add either `extension.json`, `extension.ini`, or `manifest.ini`. Invalid manifests remain visible in the Extensions window with their load error.

JSON manifests require `id`, `name`, `version`, `runtime`, and either `entry` or automatic discovery:

```json
{
  "id": "example.extension",
  "name": "Example Extension",
  "version": "1.0.0",
  "publisher": "Example Publisher",
  "runtime": "your-extension-host",
  "entry": "main.gs2",
  "capabilities": ["script-read", "file-read"],
  "ui": ["output-tab"],
  "themes": ["theme.dark"],
  "commands": ["example.run"],
  "readOnlyViews": ["example.status"],
  "api": 1
}
```

For a package whose runtime discovers files itself, omit `entry` and use `"mode": "auto"` (or `"autoDiscover": true`). RC launches the runtime from the package directory, captures its stdout/stderr, and can route readable output to a tab named from `name`. The Extensions window persists each package's enabled and output-tab state; disabling an extension requests shutdown and closes its output tab.

Requested capabilities, themes, commands/shortcuts, read-only views, and UI slots are declarative: they are shown in the extension Details view and only explicitly approved capability names are persisted for that package. Output tabs are currently the supported live view contribution; the other contribution types are safely discovered and retained for future host-owned wiring. Unknown slots are never silently wired to unrestricted RC or GTK access. Removing a package stops its runtime, closes its output tab, and deletes only that package directory after confirmation.

Use line-oriented JSON for host control messages. Human-facing output should include a non-empty `display`, `text`, or `message` field; RC suppresses protocol-only status envelopes instead of showing raw transport data.

## License

The reconstructed source code is MIT licensed. Original artwork, logos, and supplied runtime assets remain the property of their respective owners.
