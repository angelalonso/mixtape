# Application Features and Navigation Flow

- Main screen (`screen-main`):
  - Displays the primary application dashboard.
  - Automatically handles configuration lifecycle:
    - Checks for the existence of `cfg.yml`.
    - Automatically instantiates `cfg.yml` from `cfg.yml.template` if missing.
  - Provides the following primary actions:
    - **Add Mix Tape** button:
      - Opens a native GTK folder chooser dialog ("Select Mix Tape Folder").
      - If a folder is selected:
        - Logs the selected folder directory string path to the console.
        - Closes the dialog and remains on the Main screen.
      - If cancelled:
        - Closes the dialog safely and returns to the Main screen.
    - **Configuration View** action:
      - Reads, sanitizes (escapes quotes and newlines), and evaluates local JSON configuration payload data.
      - Transfers data asynchronously via `window.receiveConfig`.
    - **Exit App** action:
      - Sends a termination request to the back-end lifecycle loop, shutting down the WebView UI entirely.
  - **Keyboard Interactions**:
    - Pressing **Escape**: Routes the user directly to the Exit Confirmation screen.

- Mixes screen (`screen-mixes`):
  - Displays sub-screen options for managing mixed tracks.
  - **Keyboard Interactions**:
    - Pressing **Escape**: Dismisses the view and routes back to the Main screen.

- Exit Confirmation screen (`screen-confirm-exit`):
  - Prompts the user to confirm application exit.
  - **Keyboard Interactions**:
    - Pressing **Escape**: Cancels the exit request and safely drops back to the Main screen.

# Features

## Core

- **WebView desktop app** — single C binary embeds a GTK WebView; no Electron or Node.js required.
- **Auto-config bootstrap** — if `cfg.yml` is absent on startup it is created automatically from `cfg.yml.template` (or a sensible default is written inline).
- **Persistent JSON storage** — all state is saved to the file named in `cfg.yml → data_file` via the `saveData:` native bridge.
- **Keyboard navigation** — `Esc` moves back through screens; on the main dashboard it triggers the exit-confirmation dialog.

## Tapes (single-folder storage entries)

- **Add New Tape** — opens the native OS folder-picker dialog (GTK `SELECT_FOLDER`).
- **Auto-name** — the tape name is pre-filled with the selected folder's basename.
- **Edit name & folder** — each tape has an edit screen where the name can be changed and the folder can be replaced via the picker.
- **Tape list** — the Tapes screen shows all saved tapes with an Edit button beside each entry.
- **Persistence** — tape data (`id`, `name`, `path`) is round-tripped through the JSON data file.

## Mixes (multi-file/folder collections)

- **Add New Mix** — opens the native OS multi-select file dialog (GTK `OPEN` + `select_multiple = TRUE`) immediately so files can be picked right away.
- **Add more files** — `+ Add Files` button on the mix-edit screen opens the multi-file picker again and appends the selected files to the current list.
- **Add more folders** — `+ Add Folder` button opens a separate folder-picker dialog (GTK `SELECT_FOLDER`) and appends the chosen folder to the current list. This is necessary because GTK cannot mix file and folder selection in a single dialog.
- **Remove individual paths** — every path in the list has a dedicated Remove button that drops it from the mix without affecting the others.
- **Auto-name** — when creating a brand-new mix the name field is pre-filled with the basename of the first selected item plus " Mix".
- **Edit existing mix** — the Mixes screen shows all saved mixes with an Edit button; clicking it re-opens the edit screen pre-populated with the saved name and paths.
- **Persistence** — mix data (`id`, `name`, `paths[]`) is round-tripped through the JSON data file.

## Utilities (`server.h`)

| Function | Description |
|----------|-------------|
| `read_config_file` | Reads a file into a `ConfigData` struct; `exists = 0` if the file is absent |
| `create_config_from_template` | Copies a template file verbatim to a destination path |
| `parse_config` | Parses `data_type` and `data_file` keys from a YAML-style config string; trims trailing whitespace |
| `js_escape` | Escapes `\`, `"`, `\n`, `\r` for safe embedding in a JS double-quoted string; respects a caller-supplied buffer size limit |

## Test coverage

| Area | Tests |
|------|-------|
| Config auto-creation from template | 1 |
| Window dimensions | 2 |
| Keyboard Escape routing | 2 |
| `parse_config` — valid input, empty input, whitespace trimming, missing keys | 7 |
| `js_escape` — special chars, plain strings, newline/CR conversion, buffer overflow guard | 5 |
| Native dialog mock callbacks (tape folder, mix multi-file) | 2 |
| Mixes multi-file JS call builder — single, multiple, special chars, empty list, repeated callbacks | 9 |
| Mixes folder-picker — callback fires, JS call format, special-char escaping, empty path, repeated calls | 5 (new) |
| **Total** | **43** |
