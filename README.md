# serial-powertools

A standalone C++ tool for streaming HPGL files to vintage HP pen plotters (7470A, 7550A, DraftMaster) over a serial port. Replaces the older PyQt5 `main_qt5.py` driver with no Python runtime dependency.

Memory-aware send loop (polls `ESC.B` so the plotter's I/O buffer never overflows), port discovery with busy-port filtering, pause/abort, preset and ad-hoc commands, mid-stream insert, resume-from-percentage, and live progress reporting to a TCP server consumed by waybar or any other client.

## Stack

- **UI:** Dear ImGui (docking branch) + GLFW3 + OpenGL3
- **Serial:** [libserialport](https://sigrok.org/wiki/Libserialport)
- **Build:** Meson + Ninja, C++17
- **Platform:** Linux only

Mirrors the toolchain of the sibling [hpgl-viewer](https://github.com/schwittlick/hpgl-viewer) project.

## Build

```sh
sudo apt install build-essential meson ninja-build pkg-config \
                 libserialport-dev libglfw3-dev libgl-dev
meson setup build
meson compile -C build
```

Or run [build.sh](build.sh), which also `sudo meson install`s the binary system-wide.

## Run

```sh
./build/serial-powertools
```

Pick a port, load an HPGL file, hit **Send**. The app polls free memory between batches, so you can send arbitrarily large files without watching the buffer fill up.

For live progress on a status bar / dashboard, run [tools/progress/progress_server.py](tools/progress/progress_server.py) somewhere on your network and (optionally) export `PROGRESS_HOST`/`PROGRESS_PORT`. See [docs/progress-reporter.md](docs/progress-reporter.md) for the client side and [tools/progress/DEPLOYMENT.md](tools/progress/DEPLOYMENT.md) for the server + waybar consumer.

## Layout

```
src/
├── app/         # AppState, EtaEstimator — owns the sender thread + UI-facing state
├── discovery/   # PortDiscovery — sp_list_ports() + lsof filter + OI; probe
├── hpgl/        # Tokenizer, HpglPlotter, SerialIo, memory config (7550A)
├── reporter/    # ProgressReporter — TCP newline-JSON client
├── sender/      # AsyncSerialSender — memory-aware send loop
└── ui/          # ImGui draw functions
tools/progress/  # Python progress server, waybar consumer, reference reporter
tests/           # unit tests (top-level) + integration/ (Docker)
docs/            # progress-reporter usage
```

## Tests

**Unit tests** (no plotter, no network):

```sh
meson test -C build --print-errorlogs
```

**Integration tests** (Docker — exercises the live wire to `progress_server.py` and the full plotter conversation over a socat-emulated serial port):

```sh
./tests/integration/run.sh
```

See [tests/integration/README.md](tests/integration/README.md) for what each slice covers. Slice 2 (plotter ↔ PTY) is currently skipped in CI because `libserialport` 0.1.x rejects `/dev/pts/*` — it works against real `/dev/ttyUSB*` hardware and will re-enable once `SerialIo` migrates off libserialport.

CI runs both suites on every push and PR — see [.github/workflows/ci.yml](.github/workflows/ci.yml).
