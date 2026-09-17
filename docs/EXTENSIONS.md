# Remote Control 3 Extension Development Guide

Remote Control 3 (RC3) supports external extensions that run as independent child processes communicating with the client over standard input and output (`stdio`). Extensions can add custom editor action buttons, register background utilities, stream messages to dedicated tabs, and integrate external tools (such as linters, formatters, code generators, and diagnostic analyzers) directly into the Remote Control interface.

---

## Table of Contents
1. [Overview & Architecture](#overview--architecture)
2. [Extension Directories](#extension-directories)
3. [Manifest Specification](#manifest-specification)
   - [JSON Manifest (`extension.json`)](#json-manifest-extensionjson)
   - [INI Manifest (`extension.ini`)](#ini-manifest-extensionini)
4. [Communication Protocol](#communication-protocol)
   - [Startup Handshake](#1-startup-handshake)
   - [Window Actions (`window/action`)](#2-window-actions-windowaction)
   - [Output & Logging](#3-output--logging)
   - [Shutdown](#4-shutdown)
5. [Window Action Bindings](#window-action-bindings)
6. [Complete Examples](#complete-examples)
   - [Example 1: Python Code Linter / Formatter](#example-1-python-code-linter--formatter)
   - [Example 2: Node.js Extension](#example-2-nodejs-extension)
   - [Example 3: Standalone Executable (C/C++, Go, Rust)](#example-3-standalone-executable-cc-go-rust)
7. [Managing & Debugging Extensions](#managing--debugging-extensions)

---

## Overview & Architecture

Remote Control extensions are lightweight packages stored in an extension directory. Each extension contains:
- A manifest file: `extension.json`, `extension.ini`, or `manifest.ini`
- An entry script or executable: Python script, Node.js script, binary executable, or batch script
- Optional assets: Icons, images, or configuration files

### How It Works
1. When Remote Control opens, the `TExtensionsManager` scans the extensions directories for packages with valid manifests.
2. Enabled extensions are spawned asynchronously with piped `stdin`, `stdout`, and `stderr`.
3. Communication uses line-delimited JSON (`json-lines`) over `stdio`.
4. Remote Control routes editor actions (such as clicking a custom button on a script editor) into JSON requests sent to the extension process's standard input.
5. Standard output and error streams from the extension are captured into an inspection log and can be routed to a dedicated tab in the Remote Control window.

---

## Extension Directories

Remote Control discovers extensions in two locations:

| Scope | Location | Description |
|---|---|---|
| **Application** | `<RemoteControlDir>/extensions/<extension_folder>/` | Installed alongside the RC binary; shared by all users on the installation. |
| **User** | `%LOCALAPPDATA%\GScriptRC\extensions\<extension_folder>/`<br>*(Linux: `~/.local/share/GScriptRC/extensions/`)* | User-specific extensions; persistent across app updates. |

Each extension must be placed in its own subfolder containing the manifest file (e.g., `extensions/my-tool/extension.json`).

---

## Manifest Specification

### JSON Manifest (`extension.json`)

The recommended manifest format is `extension.json`.

```json
{
  "id": "my-script-helper",
  "name": "Script Helper",
  "version": "1.0.0",
  "publisher": "YourName",
  "runtime": "python",
  "entry": "main.py",
  "api": 1,
  "capabilities": [
    "script-read",
    "file-read"
  ],
  "ui": [
    "output-tab"
  ],
  "windowActions": [
    {
      "kind": "script-editor",
      "id": "format-script",
      "label": "Format",
      "placement": "top",
      "icon": "gtk:document-properties-symbolic"
    },
    {
      "kind": "script-editor",
      "id": "lint-script",
      "label": "Lint Code",
      "placement": "bottom",
      "icon": "gtk:dialog-information-symbolic"
    }
  ]
}
```

#### Field Reference

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | `string` | **Yes** | Unique identifier for the extension (e.g. `"my-helper"`). |
| `name` | `string` | **Yes** | Human-readable display name shown in the Extensions menu. |
| `version` | `string` | **Yes** | Semantic version string (e.g. `"1.0.0"`). |
| `publisher` | `string` | No | Author or organization name. |
| `entry` | `string` | **Yes**\* | Relative path to the main file (e.g. `"main.py"` or `"bin/tool.exe"`). *\*Not required if `mode` is `"auto"`.* |
| `runtime` | `string` | No | Executable used to launch `entry` (e.g. `"python"`, `"node"`). If left empty, `entry` is spawned directly as an executable. Set to `"gs2engine-stdio"` for built-in GS2 extensions. |
| `api` | `int` | No | Target extension API version (default is `1`). |
| `capabilities` | `string[]` | No | Declared permissions (e.g., `["script-read", "file-read"]`). Also accepted as `requestedCapabilities`. |
| `ui` | `string[]` | No | UI contributions. Include `"output-tab"` to allow output streaming into a dedicated tab in the chat notebook. Also accepted as `uiContributions`. |
| `themes` | `string[]` | No | Theme identifiers supported or contributed (e.g. `["theme.dark"]`). |
| `commands` | `string[]` | No | Command IDs registered by this extension. |
| `readOnlyViews` | `string[]` | No | Identifiers for custom read-only views. |
| `windowActions` | `object[]` | No | Toolbar action buttons injected into Remote Control editors (see below). |
| `autoDiscover` | `bool` | No | Set to `true` or set `"mode": "auto"` to automatically discover entry points. |

---

### INI Manifest (`extension.ini`)

Remote Control also supports classic INI manifests (`extension.ini` or `manifest.ini`):

```ini
id = my-script-helper
name = Script Helper
version = 1.0.0
publisher = YourName
runtime = python
entry = main.py
capabilities = script-read, file-read
ui = output-tab
windowActions = script-editor:format-script:Format, script-editor:lint-script:Lint Code
```

In `extension.ini`, `windowActions` is a comma-separated list of items formatted as:
`kind:id:label`

---

## Communication Protocol

Remote Control communicates with the extension process via standard input and standard output using **line-delimited JSON strings** (one JSON payload per line, followed by `\n`).

### 1. Startup Handshake
When the extension is launched, Remote Control sends the initial start notification over `stdin`:

```json
{"type":"start","protocol":"json-lines"}
```

Extensions can initialize resources, connect to databases, or output a welcome banner upon receiving this message.

### 2. Window Actions (`window/action`)
When a user clicks one of your extension's buttons in a script editor, Remote Control sends a request over `stdin`:

```json
{
  "id": "window-action-1",
  "method": "window/action",
  "params": {
    "action": "format-script",
    "context": {
      "windowId": "classes:player_movement",
      "kind": "script-editor",
      "title": "player_movement",
      "scriptType": "classes",
      "scriptName": "player_movement",
      "text": "//#CLIENTSIDE\nfunction onCreated() {\n  echo(\"Hello world\");\n}\n",
      "selection": ""
    }
  }
}
```

#### Context Fields
- **`windowId`**: Unique identifier for the editor instance (e.g. `"classes:player_movement"` or `"weapons:bow"`).
- **`kind`**: Window kind (currently `"script-editor"`).
- **`title`**: Window title text.
- **`scriptType`**: Type of script being edited (e.g. `"classes"` or `"weapons"`).
- **`scriptName`**: The name of the weapon, class, or script.
- **`text`**: The complete text currently in the editor buffer.
- **`selection`**: The currently highlighted/selected substring in the editor (or empty string if nothing is selected).

### 3. Output & Logging
Any line written by the extension to `stdout` or `stderr` is captured by Remote Control:

- **Raw Text:** If you write plain text (e.g. `print("Operation complete")`), it is stored in the extension's internal log (viewable via the **View Log** button in the Extensions dialog).
- **Structured Display Object:** If you output a JSON object containing a `display`, `text`, or `message` key:
  ```json
  {"display": "Lint passed: No syntax errors detected."}
  ```
  Remote Control unwraps the text for clean display.
- **Output Tab:** If the extension has `"ui": ["output-tab"]` declared (or enabled via right-clicking the extension in the menu and selecting **Send output to tab**), Remote Control creates a dedicated tab named after the extension in the chat area and streams every output line there in real time!

### 4. Shutdown
When Remote Control closes or the extension is disabled, Remote Control sends:

```json
{"type":"stop"}
```

The extension process should promptly clean up and terminate gracefully. If the process does not terminate within 750ms, Remote Control terminates it automatically.

---

## Window Action Bindings

You can inject custom buttons directly into the toolbar of Remote Control's Script Editors (classes, weapons, NPC scripts) using `windowActions`:

```json
{
  "kind": "script-editor",
  "id": "check-syntax",
  "label": "Check Syntax",
  "placement": "top",
  "icon": "gtk:system-run-symbolic"
}
```

### Action Properties

- **`kind`**: The target editor. Use `"script-editor"` to target script edit windows.
- **`id`**: Your internal action ID, passed to `params.action` when clicked.
- **`label`**: Button label text.
- **`placement`**: 
  - `"top"`: Places the button in an action bar at the top of the editor.
  - `"bottom"`: Places the button in an action bar at the bottom of the editor.
- **`icon`**: (Optional) Button icon. Supported formats:
  - `gtk:<icon-name>`: Uses a standard GTK icon name (e.g. `"gtk:document-properties-symbolic"`, `"gtk:system-run-symbolic"`, `"gtk:edit-find-symbolic"`, `"gtk:dialog-information-symbolic"`).
  - `file:<relative-path>` or `<relative-path>`: Path to a PNG or SVG icon located inside your extension folder or in an `images/` subfolder (e.g. `"file:images/myicon.png"`).

---

## Complete Examples

### Example 1: Python Code Linter / Formatter

This complete example creates an extension that checks scripts for common GS2 issues and reports results both to the extension log and the dedicated output tab.

#### Directory Layout
```
extensions/
  gs2-analyzer/
    extension.json
    main.py
```

#### `extensions/gs2-analyzer/extension.json`
```json
{
  "id": "gs2-analyzer",
  "name": "GS2 Code Analyzer",
  "version": "1.0.0",
  "publisher": "Community",
  "runtime": "python",
  "entry": "main.py",
  "api": 1,
  "capabilities": [
    "script-read"
  ],
  "ui": [
    "output-tab"
  ],
  "windowActions": [
    {
      "kind": "script-editor",
      "id": "analyze-code",
      "label": "Analyze GS2",
      "placement": "top",
      "icon": "gtk:system-run-symbolic"
    }
  ]
}
```

#### `extensions/gs2-analyzer/main.py`
```python
import sys
import json

def analyze_script(script_name, code):
    lines = code.splitlines()
    issues = []
    
    # Simple demonstration checks
    has_clientside = any("//#CLIENTSIDE" in line for line in lines)
    for idx, line in enumerate(lines, start=1):
        if "sleep(" in line and not has_clientside:
            issues.append(f"Line {idx}: sleep() used in server-side script without clientside context.")
        if "setstring" in line:
            issues.append(f"Line {idx}: Deprecated GS1 function 'setstring' detected.")
    
    return issues

def main():
    # Ensure stdout flushes immediately after every print
    sys.stdout.reconfigure(line_buffering=True)
    
    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        
        msg_type = msg.get("type")
        method = msg.get("method")
        
        # 1. Startup handshake
        if msg_type == "start":
            print(json.dumps({"display": "GS2 Code Analyzer extension started."}))
            continue
        
        # 2. Stop notification
        if msg_type == "stop":
            print(json.dumps({"display": "Shutting down GS2 Code Analyzer."}))
            break
        
        # 3. Editor Window Action
        if method == "window/action":
            params = msg.get("params", {})
            action = params.get("action")
            context = params.get("context", {})
            
            if action == "analyze-code":
                script_name = context.get("scriptName", "Untitled")
                script_type = context.get("scriptType", "script")
                code = context.get("text", "")
                
                print(json.dumps({"display": f"--- Analyzing {script_type}: {script_name} ---"}))
                
                issues = analyze_script(script_name, code)
                if not issues:
                    print(json.dumps({"display": f"✓ {script_name}: No issues found! Clean script."}))
                else:
                    for issue in issues:
                        print(json.dumps({"display": f"⚠ {issue}"}))
                    print(json.dumps({"display": f"Found {len(issues)} issue(s) in {script_name}."}))

if __name__ == "__main__":
    main()
```

---

### Example 2: Node.js Extension

#### `extensions/node-formatter/extension.json`
```json
{
  "id": "node-formatter",
  "name": "Node Script Formatter",
  "version": "1.0.0",
  "publisher": "Developer",
  "runtime": "node",
  "entry": "index.js",
  "api": 1,
  "ui": [
    "output-tab"
  ],
  "windowActions": [
    {
      "kind": "script-editor",
      "id": "count-lines",
      "label": "Count Stats",
      "placement": "bottom",
      "icon": "gtk:dialog-information-symbolic"
    }
  ]
}
```

#### `extensions/node-formatter/index.js`
```javascript
const readline = require('readline');

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
  terminal: false
});

rl.on('line', (line) => {
  line = line.trim();
  if (!line) return;

  try {
    const msg = JSON.parse(line);

    if (msg.type === 'start') {
      console.log(JSON.stringify({ display: 'Node Script Formatter active.' }));
    } else if (msg.type === 'stop') {
      process.exit(0);
    } else if (msg.method === 'window/action') {
      const { action, context } = msg.params;
      if (action === 'count-lines') {
        const text = context.text || '';
        const lineCount = text.split('\n').length;
        const charCount = text.length;
        console.log(JSON.stringify({
          display: `Stats for ${context.scriptName}: ${lineCount} lines, ${charCount} characters.`
        }));
      }
    }
  } catch (err) {
    console.error('Error processing message:', err);
  }
});
```

---

### Example 3: Standalone Executable (C/C++, Go, Rust)

For compiled standalone executables (e.g. `tool.exe` on Windows or `tool` on Linux), omit the `runtime` field or leave it empty, and point `entry` directly to your binary:

```json
{
  "id": "native-tool",
  "name": "Native Performance Tool",
  "version": "1.2.0",
  "entry": "native_tool.exe",
  "api": 1,
  "ui": ["output-tab"],
  "windowActions": [
    {
      "kind": "script-editor",
      "id": "run-fast",
      "label": "Fast Check",
      "placement": "top",
      "icon": "gtk:system-run-symbolic"
    }
  ]
}
```

---

## Managing & Debugging Extensions

1. **Accessing the Extensions Manager:**
   - In Remote Control, click **Tools** → **Extensions** in the menu bar, or click the **Extensions** graphical button (icon #13) on the toolbar.
2. **Empty State:**
   - If no extensions are installed in your `extensions/` directories, Remote Control will display an information icon with **"No extensions installed"**.
3. **Enabling / Disabling:**
   - Toggle the **Enabled** checkbox next to any extension to start or stop its background process.
4. **Context Menu Actions:**
   - Right-click any extension row in the manager to:
     - **Enable / Disable** the extension.
     - **Send output to tab / Stop sending output to tab**: Automatically creates a dedicated tab in the chat notebook to stream live extension output.
5. **Action Buttons:**
   - **Open Folder** (`document-open-symbolic`): Opens the extension's folder in your system file explorer.
   - **View Log** (`view-list-symbolic`): Opens a modal window showing recent unbuffered `stdout` and `stderr` output.
   - **Details** (`dialog-information-symbolic`): Displays full manifest properties and capabilities.
   - **Remove** (`user-trash-symbolic`): Prompts to terminate the process and uninstall the extension package from disk.
