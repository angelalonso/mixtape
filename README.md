# MixTape

A GTK desktop application for managing and applying rsync backup configurations. It pairs a Mix (a set of source files or folders) with a Tape (a mounted destination folder) into a named Mix-Tape, then copies the sources to the destination using rsync. The UI is a single C binary embedding a GTK WebView; no Electron or Node.js is required.

## Prerequisites

Install the GTK and WebKit2GTK development libraries:

```
sudo apt install libwebkit2gtk-4.1-dev build-essential
```

Download the vendored single-file dependencies into the project directory:

```
curl -O https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c
curl -O https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.h
curl -O https://raw.githubusercontent.com/webview/webview/master/webview.h
```

## TL;DR — Makefile targets

| Command      | What it does                                              |
|--------------|-----------------------------------------------------------|
| `make`       | Compile the application binary (`mixtape`)                |
| `make run`   | Compile (if needed) and launch the application            |
| `make test`  | Compile and run the test suite (`test_runner`)            |
| `make clean` | Remove the compiled binaries and the generated `cfg.yml`  |

## Configuration

On first run, `cfg.yml` is created automatically from `cfg.yml.template` (or written inline if the template is absent). Edit `cfg.yml` to change behaviour without recompiling:

| Key                  | Default     | Description                                                       |
|----------------------|-------------|-------------------------------------------------------------------|
| `data_type`          | `file`      | Storage backend. Only `file` is supported.                        |
| `data_file`          | `data.json` | Path to the JSON file that persists mixes, tapes, and mix-tapes.  |
| `tape_check_interval`| `5`         | Seconds between automatic tape availability checks.               |
| `log_level`          | `2`         | `0` off, `1` errors, `2` info, `3` verbose (all rsync output).   |
| `max_rsync_workers`  | `0`         | Parallel rsync threads. `0` = one thread per logical CPU.         |
