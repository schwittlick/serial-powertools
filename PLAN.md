# C++ Port of `serial_powertools/qt/main_qt5.py`

## Context

`main_qt5.py` is a PyQt5 desktop tool that streams HPGL files to vintage HP pen plotters over a serial port. It manages connection, a memory-aware async send loop (the plotter has a small I/O buffer that must be polled with `ESC.B` before more commands are pushed), pause/abort, a preset-command panel, and remote progress reporting to a configurable progress server (default `localhost:9876`, see `tools/progress/DEPLOYMENT.md`). The Python implementation pulls in a non-trivial slice of the `cursor` package (`cursor.hpgl`, `cursor.hpgl.plotter`, `cursor.tools.discovery`, `cursor.timer`), which makes it awkward to deploy standalone — it needs the whole repo installed.

Goal: a self-contained C++ application that replaces this tool with **no Python runtime dependency**, exact wire-protocol parity with the plotter, and a UI built on the same stack as the sibling [hpgl-viewer](file:///home/marcel/dev/hpgl-viewer) project (Dear ImGui + GLFW + OpenGL3) so the two stay consistent.

**Decisions:**
- **Stack:** Dear ImGui (docking branch) + GLFW3 + OpenGL3 + `libserialport` for serial I/O + raw BSD sockets for the TCP progress client. Meson + ninja. C++17. Mirrors hpgl-viewer's build exactly.
- **Why not Qt6:** The only thing Qt would add is `QSerialPort`, which `libserialport` replaces in ~100 LOC. Everything else (file dialog, TCP socket, JSON, threads, widgets) is simpler or equally clean in ImGui+`std::*`. Staying on the hpgl-viewer stack means one toolchain, one mental model, and small/fast builds.
- **Platform:** Linux only. Keep `lsof`-based busy-port filtering for parity with the Python `discover()`.
- **Progress reporter:** C++ speaks the same TCP newline-JSON wire protocol directly via raw sockets. The Python implementations of the reporter client, server, and waybar consumer live in `tools/progress/` for use by other (non-C++) jobs and for end-to-end testing. Deployment docs in `tools/progress/DEPLOYMENT.md`.
- **Scope:** Full feature parity with `main_qt5.py` — preset buttons, manual command, mid-stream insert, port discovery probe, resume-from-percentage. *Excluded:* `main_debugger_qt5.py`, `bruteforce_qt5.py`, `serial_inspector_qt5.py` standalone debugger pieces.

## Reference source repository

The Python implementation to port lives **in this project**, under `reference/` (frozen snapshot copied from `/home/marcel/dev/cursor/cursor/cursor/` — do not edit). All Python paths in this plan are relative to the project root and resolve to that snapshot. The C++ port goes alongside it under `src/`.

Layout at start:
```
serial-powertools/
├── PLAN.md                          # this file
├── tools/progress/                  # first-class part of the project — Python, deployed/run as-is
│   ├── progress_reporter.py         # client library — bundled for use by other Python jobs
│   ├── progress_server.py           # server (runs on whichever host holds job state)
│   ├── waybar_progress.py           # waybar custom-module consumer (runs on the desktop)
│   └── DEPLOYMENT.md                # how to deploy server + waybar + use the reporter
└── reference/                       # read-only Python source — protocol/behavior reference only
    ├── hpgl/__init__.py
    ├── hpgl/hpgl_tokenize.py
    ├── hpgl/plotter/plotter.py
    ├── hpgl/plotter/memory_config.py
    ├── timer.py
    ├── tools/discovery.py
    └── tools/serial_powertools/
        ├── seriallib.py
        └── qt/
            ├── main_qt5.py
            └── serial_inspector_qt5.py
```

`tools/progress/` is a **first-class part of this project** — the Python progress system is deployed as-is, not ported. The C++ app implements its own client that speaks the same TCP+JSON wire protocol so it can integrate with the same server. Other Python jobs in the user's workflow can `import` from `tools/progress/progress_reporter.py` directly.

`reference/` is a frozen snapshot of the Python source we're porting *from* — do not modify.

## Kickoff — what to do on first read

1. Read every file in **Critical files to reference while porting** before writing any C++.
2. Initialize the project skeleton from **Build system** (`meson.build`, `src/`, `tests/`, `meson_options.txt`).
3. Implement in the order given in **Recommended build order** — pure logic first, UI last.
4. After each component, run `meson test -C build` (unit tests) and `meson compile -C build` (must stay green).
5. Tick the **Behavior parity checklist** items as you finish them — they're the acceptance criteria.
6. Finish with the **Verification** section. Stop and ask the user if a step requires the physical plotter or a running progress server.

## Source mapping

All Python paths below are absolute (rooted at the reference repo).

| Python source | C++ target | Notes |
|---|---|---|
| `reference/hpgl/__init__.py` | `src/hpgl/Constants.h` | ESC sequences, CR/LF/LB_TERMINATOR as `constexpr` |
| `reference/hpgl/__init__.py` (`read_until_char`) | `src/hpgl/SerialIo.{h,cpp}` | Reads from `libserialport` handle until terminator, with timeout (`std::chrono::steady_clock`) |
| `reference/hpgl/hpgl_tokenize.py` | `src/hpgl/Tokenizer.{h,cpp}` | LB-aware split — pure logic, no deps |
| `reference/hpgl/plotter/plotter.py` | `src/hpgl/HpglPlotter.{h,cpp}` | identify/free_memory/abort/etc. — wraps a `libserialport` port handle |
| `reference/hpgl/plotter/memory_config.py` (HP7550A only) | `src/hpgl/Hp7550aMemoryConfig.{h,cpp}` | DraftMaster path not needed |
| `reference/tools/discovery.py` | `src/discovery/PortDiscovery.{h,cpp}` | `sp_list_ports()` (libserialport) + `lsof` filter + per-port `OI;` probe via `std::thread` |
| `reference/timer.py` | inline `std::chrono::steady_clock` | No standalone class needed |
| `reference/tools/serial_powertools/seriallib.py` (`AsyncSerialSender`) | `src/sender/AsyncSerialSender.{h,cpp}` | Plain class with a `std::thread` worker + `std::mutex` + `std::condition_variable`. Direct port of the Python `threading.Thread`-based design |
| `reference/tools/serial_powertools/seriallib.py` (`concat_commands`) | static helper in `Tokenizer` | LB uses `\x03` terminator instead of `;` |
| `reference/tools/serial_powertools/qt/serial_inspector_qt5.py` | `src/app/AppState.{h,cpp}` | Plain struct + methods that own the plotter, the sender thread, the log buffer, the file-send state. Polled by ImGui each frame (no Qt signal/slot equivalent — just read fields and act) |
| `tools/progress/progress_reporter.py` | `src/reporter/ProgressReporter.{h,cpp}` | Raw BSD sockets (`<sys/socket.h>`, `<netdb.h>`) + handwritten JSON line — must speak the **same** wire protocol so it interoperates with the Python `progress_server.py` |
| `reference/tools/serial_powertools/qt/main_qt5.py` (`SerialInspectorGUI`) | `src/ui/Ui.{h,cpp}` | ImGui draw functions (`drawControls`, `drawOutputLog`) called from `main.cpp`'s render loop |
| `reference/tools/serial_powertools/qt/main_qt5.py` (`estimate_remaining_time`) | `src/app/EtaEstimator.{h,cpp}` | 500-sample sliding window |

## Wire protocol — must match exactly

The Python tool already works; C++ must produce byte-identical traffic.

**ESC sequences** (`ESC` = `0x1B`):
- `ESC.A` — identify (`0x1B 0x2E 0x41`)
- `ESC.B` — query free I/O buffer (`0x1B 0x2E 0x42`)
- `ESC.K` — abort graphics (`0x1B 0x2E 0x4B`)
- `ESC.L` — wait / drain buffer (`0x1B 0x2E 0x4C`)
- `ESC.R` — reset device
- `ESC.O` — extended status
- `ESC.T<io>;<polygon>;<char>;<replot>;<vector>:` — memory alloc (7550A)
- `ESC.@<io>:` — logical buffer size

**Response terminator:** CR (`0x0D`) for all queries. Default timeout 1.0 s.

**Tokenization rules** (see `reference/hpgl/hpgl_tokenize.py`):
1. Split input by `\x03` (LB_TERMINATOR) into batches.
2. For each batch: if it contains `"LB"`, split off the pre-LB part on `;` and keep the `LB…\x03` token whole; else split the whole batch on `;`.
3. Drop empty tokens.

**`concat_commands` rules** (see `reference/tools/serial_powertools/seriallib.py`):
- Join with `;` for normal commands, terminate the joined string with `;`.
- A command starting with `LB` is appended with a trailing `\x03` instead of `;`.

**Model-specific init** (HP 7550A only):
- After `identify()` returns `"7550A"`, send `ESC.T12752;4;0;0;44:` followed by `ESC.L` + read-until-CR, then `ESC.@12752:` + `ESC.L` + read-until-CR.

**Discovery probe:** send `OI;` (classic HPGL, **not** `ESC.A` — HP7470A/Roland DXY don't respond to ESC.A), read until CR with 0.5 s timeout. Non-empty stripped response = plotter detected. Optionally follow with `OH;` to capture dimensions.

## Threading model

Three threads, mirroring the Python design. No Qt — plain `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic` (same pattern hpgl-viewer uses for its parser worker, see `hpgl-viewer/src/main.cpp:178-203`).

1. **Main thread** — GLFW window, ImGui frame loop, owns `AppState`. Every frame reads the latest values from worker threads via atomics / mutex-protected snapshots; never blocks on I/O.
2. **Sender thread** (`AsyncSerialSender`) — owns the `libserialport` port handle, runs the batch loop:
   ```
   loop until stopped:
     if commands available and not paused:
       batch = commands[i : i+batch_size]
       if do_software_handshake:
         poll plotter.freeMemory() until >= concat_bytes(batch) + safety
       plotter.write(concat_commands(batch))
       i += batch.size()
       progressIndex.store(i, std::memory_order_release)
     std::this_thread::sleep_for(10ms)
   ```
   Mutex-protected fields: `commands` (vector), `commandBatch`, `memoryLimit`, `currentIndex`, control flags. Use `std::condition_variable` for pause/resume to avoid busy-waiting where the Python version does (free improvement — see parity-checklist note).
3. **Discovery thread** (one-shot `std::thread`, launched on Refresh) — runs `PortDiscovery::discover()` which spawns one short-lived `std::thread` per candidate port for the `OI;` probe, joins them, and stores the result vector behind a `std::mutex` for the main thread to pick up on the next frame. Use `glfwPostEmptyEvent()` to wake the ImGui loop immediately when the result lands.

Main thread → worker communication uses a small queue protected by `std::mutex` (e.g., command insertions). Worker → main uses `std::atomic` for scalar progress + a mutex-protected `std::deque<std::string>` for the log buffer.

`ProgressReporter` runs its own `std::thread` worker with `std::condition_variable::wait_for(1s, [&]{ return dirty || stop; })` — mirrors the Python `threading.Event::wait(timeout)` semantics exactly.

## UI layout (ImGui)

Match the Python layout 1:1 using ImGui docking (same setup as hpgl-viewer — see `hpgl-viewer/src/main.cpp:144-152`). One viewport, two docked windows side-by-side:

```
GLFW window (initial 1400×900, resizable)
└── ImGui dockspace (docking branch)
    ├── "Controls" (left, ~30% width)
    │   ├── // Inspector section
    │   ├── ImGui::Combo("Port", ...)        // port dropdown
    │   ├── ImGui::Combo("Baud", ...)        // baud dropdown
    │   ├── ImGui::Button("Refresh")  ImGui::Button("Connect")  ImGui::TextUnformatted(statusStr)
    │   ├── ImGui::InputText("##cmd", buf)  ImGui::Button("Send")
    │   │
    │   ├── // Preset grid — 2 per row, just emit buttons in pairs
    │   ├── for each preset:  if (ImGui::Button("PU;")) sendCommand("PU;");
    │   │       presets: IN; OA; OE; OH; OI; PU; PD; PA0,0; PA10000,10000;
    │   │                PA<random>; ESC.R; ESC.K
    │   ├── // Pen selects (3 per row): SP0 … SP8
    │   ├── // Velocity (3 per row): VS1 VS5 VS10 VS25 VS50 VS100
    │   │
    │   ├── // Send-file section
    │   ├── ImGui::TextUnformatted(selectedFile)
    │   ├── ImGui::Button("Select")  ImGui::Button("Send")  ImGui::Button("Pause")  ImGui::Button("Abort")
    │   ├── ImGui::SliderInt("Batch", &batch, 1, 40)
    │   ├── ImGui::InputFloat("Start %", &startPct, 0.0f, 100.0f, "%.0f")
    │   ├── ImGui::SliderInt("Memory", &memLimit, 32, 1024)
    │   ├── ImGui::ProgressBar(progressFrac)  ImGui::SameLine()  ImGui::TextUnformatted(etaStr)
    │   │
    │   └── // Insert-command row
    │       ImGui::InputText("##insert", insertBuf)  ImGui::Button("Insert Command")
    │
    └── "Output" (right, ~70% width)
        ├── ImGui::PushFont(monospace)
        ├── ImGui::BeginChild("##scroll", ImVec2(0, -lineHeight), true, ImGuiWindowFlags_HorizontalScrollbar)
        │     for (auto& line : logBuffer) ImGui::TextUnformatted(line.c_str());
        │     if (autoScroll) ImGui::SetScrollHereY(1.0f);
        ├── ImGui::EndChild()
        └── ImGui::Button("Clear output")
```

File picker — use `kdialog --getopenfilename '<startDir>' '*.hpgl *.plt *.hgl'` via `popen` exactly as hpgl-viewer does (see `hpgl-viewer/src/main.cpp:29-50`). Lift that helper directly.

Logging is a `std::deque<std::string>` protected by a `std::mutex`. Any thread can `pushLog(line)`; the main thread iterates a snapshot each frame. Cap at e.g. 10k lines to bound memory.

Pump GLFW events with `glfwWaitEventsTimeout(1.0/60.0)` (energy-efficient — same as hpgl-viewer). Worker threads call `glfwPostEmptyEvent()` after meaningful state changes so the UI repaints promptly.

## ETA estimator

Port `estimate_remaining_time()` verbatim: ring buffer of 500 `(steady_clock::time_point, double progress)` samples; rate = (latest.progress − oldest.progress) / Δseconds; ETA = (1 − latest.progress) / rate. Format as `mm:ss` or `hh:mm:ss`.

## ProgressReporter (TCP)

**Protocol is TCP**, newline-delimited JSON. The server host and port are **configurable** — the C++ implementation must read them from `PROGRESS_HOST` / `PROGRESS_PORT` env vars (defaults `localhost` / `9876`) to match the Python reporter's behavior. Reporters open a fresh TCP connection per send, write one JSON line, close. The C++ port must do the same so it interoperates with `progress_server.py` byte-for-byte.

Message format (only `report` is sent by the reporter — `clear` / `clear_done` / `query` / `ack_notify` are sent by waybar):

```json
{"type":"report","id":"<8-hex>","label":"<file>","progress":0.42,"done":false}
```

- `id`: 8-char string (defaults to `uuid.uuid4()[:8]` in Python — match with `QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)`).
- `progress`: clamped to `[0.0, 1.0]`.
- `done`: `true` only on the final send after `finish()`; `false` otherwise.
- Trailing `"\n"` is required.

Send semantics (mirror Python):
- One TCP connection per send: `connect → sendall(line) → close`. Connect timeout 3 s.
- A worker thread waits on a `dirty` event with a `min_interval` (1.0 s) timeout. When it wakes, if ≥ `min_interval` has passed since last send, it sends.
- Server unreachable = silent drop, keep trying on next tick. Never crash the send pipeline.
- `finish()` sets `progress=1.0, done=true`, signals dirty + stop, joins worker (≤5 s), and the worker does one final send to guarantee the `done=true` line reaches the server.

API:
```cpp
class ProgressReporter {
public:
    // host/port: empty / 0 = read PROGRESS_HOST/PROGRESS_PORT env (defaults localhost:9876)
    ProgressReporter(std::string label,
                     std::string jobId = "",
                     std::string host = "",
                     uint16_t port = 0,
                     double minIntervalSec = 1.0);
    ~ProgressReporter();   // calls finish() if not already
    void start();          // launches worker thread, sends initial 0.0 report
    void report(double p); // thread-safe, non-blocking
    void finish();         // sets 1.0, done=true, joins worker (≤5 s)
};
```

Implementation notes:
- Raw BSD sockets: `getaddrinfo()` → `socket()` → `connect()` (with `SO_SNDTIMEO`/`SO_RCVTIMEO` 3 s) → `send()` the line → `close()`. New socket per message — Python does the same and the server expects it.
- JSON: only one message type sent (`report`); hand-formatted with `snprintf` is fine and avoids a dep. Escape `label` for `"` and `\` minimally (the only realistic risk).
- Worker: `std::thread` + `std::mutex` + `std::condition_variable`. The wait loop is:
  ```cpp
  std::unique_lock lk(mu);
  cv.wait_for(lk, std::chrono::duration<double>(minInterval),
              [&]{ return dirty || stop; });
  dirty = false;
  ```
  Mirrors Python's `threading.Event.wait(timeout)` semantics exactly.
- Job id: 8 hex chars. Use `std::random_device` + `std::mt19937_64` to produce 32 random bits, hex-format with `snprintf("%08x", ...)`.

Cross-check after implementing: run the C++ reporter and the Python `progress_reporter.py` against the same `progress_server.py` and confirm the server logs are identical line-for-line.

## Build system

Meson + ninja, C++17, mirroring hpgl-viewer's structure (`hpgl-viewer/meson.build`).

System packages (Arch): `glfw-wayland` (or `glfw-x11`), `mesa`, `libserialport`, `meson`, `ninja`. ImGui comes in as a meson wrap (`subprojects/imgui.wrap`, same `revision = docking` line that hpgl-viewer uses).

`meson.build` skeleton:

```meson
project('serial-powertools', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++17', 'warning_level=2'])

# ── Dependencies ──────────────────────────────────────────────────────────────
glfw_dep    = dependency('glfw3')
gl_dep      = dependency('gl')
serial_dep  = dependency('libserialport')
threads_dep = dependency('threads')

# ImGui via wrap (subprojects/imgui.wrap — same revision=docking as hpgl-viewer)
imgui_proj = subproject('imgui', default_options : ['default_library=static'])
imgui_dep  = imgui_proj.get_variable('imgui_dep')

inc = include_directories('src')

# ── Pure-logic libs (no UI/serial deps — easy to unit-test) ───────────────────
tokenizer_lib = static_library('plt_tokenizer',
  sources : ['src/hpgl/Tokenizer.cpp'],
  include_directories : inc)

memcfg_lib = static_library('plt_memcfg',
  sources : ['src/hpgl/Hp7550aMemoryConfig.cpp'],
  include_directories : inc)

eta_lib = static_library('plt_eta',
  sources : ['src/app/EtaEstimator.cpp'],
  include_directories : inc)

# ── Serial / plotter / discovery ──────────────────────────────────────────────
hpgl_lib = static_library('plt_hpgl',
  sources : ['src/hpgl/SerialIo.cpp', 'src/hpgl/HpglPlotter.cpp'],
  link_with : [tokenizer_lib, memcfg_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])

discovery_lib = static_library('plt_discovery',
  sources : ['src/discovery/PortDiscovery.cpp'],
  link_with : [hpgl_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])

sender_lib = static_library('plt_sender',
  sources : ['src/sender/AsyncSerialSender.cpp'],
  link_with : [hpgl_lib, tokenizer_lib],
  include_directories : inc,
  dependencies : [threads_dep])

reporter_lib = static_library('plt_reporter',
  sources : ['src/reporter/ProgressReporter.cpp'],
  include_directories : inc,
  dependencies : [threads_dep])

app_lib = static_library('plt_app',
  sources : ['src/app/AppState.cpp'],
  link_with : [hpgl_lib, sender_lib, discovery_lib, reporter_lib, eta_lib],
  include_directories : inc,
  dependencies : [serial_dep, threads_dep])

# ── Executable ────────────────────────────────────────────────────────────────
executable('serial-powertools',
  sources : ['src/main.cpp', 'src/ui/Ui.cpp'],
  link_with : [app_lib, hpgl_lib, sender_lib, discovery_lib, reporter_lib, eta_lib, tokenizer_lib],
  include_directories : inc,
  dependencies : [glfw_dep, gl_dep, imgui_dep, serial_dep, threads_dep],
  install : true)

subdir('tests')
```

`subprojects/imgui.wrap`:
```ini
[wrap-git]
url = https://github.com/ocornut/imgui.git
revision = docking
depth = 1
patch_directory = imgui

[provide]
imgui = imgui_dep
```

Copy `subprojects/packagefiles/imgui/` from hpgl-viewer (the meson packagefile that builds ImGui + the GLFW + OpenGL3 backends as a static lib).

`tests/meson.build` adds executables that exercise the pure-logic libs. Run with `meson test -C build`.

Suggested directory layout for the new repo:
```
serial-powertools/
├── meson.build
├── meson_options.txt          # e.g. -Dbuild_tests=true
├── subprojects/
│   ├── imgui.wrap
│   └── packagefiles/imgui/    # copied from hpgl-viewer
├── src/
│   ├── main.cpp               # GLFW + ImGui setup + frame loop (model after hpgl-viewer/src/main.cpp)
│   ├── ui/                    # Ui.{h,cpp} — pure ImGui draw functions, no state of their own
│   ├── app/                   # AppState, EtaEstimator — owns plotter + sender + log buffer
│   ├── hpgl/                  # Constants, Tokenizer, SerialIo, HpglPlotter, Hp7550aMemoryConfig
│   ├── sender/                # AsyncSerialSender (std::thread worker)
│   ├── discovery/             # PortDiscovery (libserialport + lsof + per-port OI; probe)
│   └── reporter/              # ProgressReporter (BSD sockets, TCP, std::thread worker)
└── tests/
    ├── meson.build
    ├── test_tokenizer.cpp
    ├── test_concat_commands.cpp
    └── test_eta_estimator.cpp
```

## Recommended build order

1. **Project skeleton** — copy `meson.build`, `subprojects/imgui.wrap`, and `subprojects/packagefiles/imgui/` from hpgl-viewer; get an empty window up with the docking layout from `hpgl-viewer/src/main.cpp:144-152`. Verifies the build pipeline before any porting work.
2. **Constants + Tokenizer + concat_commands** — pure logic, unit-testable with no external deps. Validate against the Python tokenizer using golden inputs (label commands with `\x03`).
3. **HpglPlotter + SerialIo** — wraps a `libserialport` port handle. Test against a real plotter or a `socat` pty pair feeding canned responses.
4. **AsyncSerialSender** — port the run loop, lock semantics, batch sizing, pause/abort, mid-stream insert. Smoke-test by feeding a long synthetic command list.
5. **PortDiscovery** — `sp_list_ports()` (libserialport) + `lsof` filter + per-port probe threads sending `OI;`.
6. **ProgressReporter** — verify against `tools/progress/progress_server.py` (run it locally on `localhost:9876` for the test).
7. **AppState + EtaEstimator + Ui** — wire everything to the ImGui draw functions last. `AppState` is the single owner of plotter, sender, log buffer, file-send state; `Ui::drawControls(app)` and `Ui::drawOutput(app)` read/mutate it directly in the main thread.

## Critical files to reference while porting

- `reference/tools/serial_powertools/qt/main_qt5.py` — 572 lines, the widget tree + ETA estimator + signal wiring
- `reference/tools/serial_powertools/qt/serial_inspector_qt5.py` — 182 lines, the QObject bridge contract
- `reference/tools/serial_powertools/seriallib.py` — 191 lines, the AsyncSerialSender semantics (lock scope, batching, memory handshake)
- `tools/progress/progress_reporter.py` — 121 lines, the TCP wire protocol (now in `tools/progress/`, not `reference/`)
- `tools/progress/progress_server.py` — the server side that defines the full message vocabulary
- `tools/progress/DEPLOYMENT.md` — server/waybar/usage setup
- `reference/hpgl/plotter/plotter.py` — 110 lines, the exact ESC sequence flow
- `reference/hpgl/plotter/memory_config.py` — 7550A constants
- `reference/hpgl/hpgl_tokenize.py` — 26 lines, the LB-aware split
- `reference/hpgl/__init__.py` — constants + `read_until_char`
- `reference/tools/discovery.py` — 114 lines, the `OI;` probe + `lsof` filter

## Behavior parity checklist (must match Python)

- [ ] Sending `LB<text>` produces `LB<text>\x03` on the wire (not `LB<text>;`).
- [ ] `concat_commands` terminates the joined batch with `;` for normal commands.
- [ ] `free_memory()` returns 0 (not throws) when the response isn't an integer.
- [ ] On `SerialException`-equivalent during read, the port is closed, slept 0.5 s, and reopened — same recovery as `HPGLPlotter.read_until`.
- [ ] HP 7550A memory init runs exactly once after identify, before any user commands.
- [ ] Discovery probes use `OI;` (not `ESC.A`).
- [ ] Discovery filters out ports currently held by other processes via `lsof`.
- [ ] AsyncSerialSender's `insert_commands` injects at the *current* send position, not at the end.
- [ ] AsyncSerialSender's `add_commands(curr_index=N)` skips the first N tokens (resume-from-percentage).
- [ ] Pause busy-waits 100 ms between checks; main loop sleeps 10 ms between batches.
- [ ] ProgressReporter: 8-char hex job id when none provided; 1 s minimum interval; final send on `finish()` includes `done=true`.

## Verification

End-to-end (requires real plotter, e.g. HP 7550A on `/dev/ttyUSB0`):

```bash
meson setup build
meson compile -C build
./build/serial-powertools
```

1. Open the app → click **Refresh** → port appears with detected model.
2. Select port + baud → **Connect** → status shows "Connected", output log shows model identify response.
3. Click **PU;** preset → pen lifts. Click **PA0,0;** → carriage moves to origin.
4. **Select file** → choose a small HPGL file → **Send**. Progress bar advances, ETA label updates, output log shows batch sends.
5. **Pause** mid-send → motion halts within one batch. **Pause** again → resumes.
6. **Abort** → motion stops immediately, plotter receives `ESC.K`.
7. Start-percentage `50` + new file + **Send** → only the back half of the file plays.
8. Start `tools/progress/progress_server.py` locally, point the app at `PROGRESS_HOST=localhost`, and confirm: a) the server logs report lines during the send, b) `tcpdump -i lo -A 'tcp port 9876'` shows the C++ traffic byte-for-byte matching what `progress_reporter.py` would have produced for the same job.
9. Resize window, type into manual command field, hit **Insert Command** mid-send — injected command appears in the stream without disrupting the queue.

Offline (no plotter):

- Run unit tests: `ninja test` — tokenizer round-trips, `concat_commands` golden vectors (including LB), ETA estimator math.
- Use `socat -d -d pty,raw,echo=0 pty,raw,echo=0` to create a pty pair; point the app at one end, feed canned `\r`-terminated responses from a script on the other to test identify / free_memory parsing and the read-until-CR timeout path.

## Out of scope

- `main_debugger_qt5.py` — separate low-level debugger UI (462 lines).
- `bruteforce_qt5.py` — connection-parameter brute forcer (the `start_bruteforce_progress` method on `SerialInspector` is **not** ported).
- `serial_inspector.py` / `bruteforce.py` (non-qt CLI scripts).
- HP 7595A / 7596A `DraftMasterMemoryConfig` — commented out in the Python source.
- Windows / macOS support.
- Persisting UI settings between sessions — the Python doesn't either. (ImGui will autosave its layout to `imgui.ini`, which is fine.)
