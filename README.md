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
| `max_rsync_workers`  | `0`         | Parallel rsync processes. `0` = one per logical CPU.              |

## Parallelism model

When you press Apply on a Mix-Tape, the source paths from the Mix are distributed across worker slots using **round-robin assignment**, then all workers are dispatched concurrently to the C thread pool.

### Why round-robin instead of chunking

The previous approach split paths into fixed-size batches of up to 20 and ran batches sequentially. This meant that with, say, 21 paths and 4 available threads, 3 threads sat idle while 2 batches ran one after the other.

Round-robin distributes paths as evenly as possible across exactly `max_rsync_workers` slots (or as many slots as there are paths, whichever is smaller), then fires all of them at once. The C thread pool picks them up immediately. No thread sits idle waiting for a previous batch to finish.

### How paths are distributed

Given P source paths and W workers (W = min(max_rsync_workers, P)):

```
path[0]  -> command 0
path[1]  -> command 1
...
path[W-1] -> command W-1
path[W]   -> command 0   (wraps)
path[W+1] -> command 1
...
```

Each command receives `floor(P/W)` paths, with the first `P mod W` commands receiving one extra path. No command is ever empty.

### Note on rsync's own parallelism

rsync itself is single-threaded per invocation and has no built-in parallel flag. Tools like GNU `parallel` can wrap it, but they are not guaranteed to be present. MixTape therefore achieves parallelism by launching multiple rsync processes simultaneously, one per worker bucket.

### Practical guidance for `max_rsync_workers`

| Scenario | Suggested value |
|---|---|
| Single spinning hard drive destination | `1` — concurrent writes thrash the head |
| SSD or NVMe destination | `0` (auto, = CPU count) or `4`–`8` |
| Network destination (NAS, SMB) | `2`–`4`; higher values often saturate the link without saving time |
| Very large number of small files | Higher values help because rsync startup overhead dominates |
| Small number of large files | `1` or `2`; parallelism helps less than sequential throughput |
