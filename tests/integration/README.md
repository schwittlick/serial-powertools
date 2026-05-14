# Integration tests

Docker-based integration tests that exercise the bits the unit tests can't reach: the live wire to a `progress_server` and a full plotter conversation over a serial port.

Two slices:

| Slice | What it covers                                                                                       |
|------:|------------------------------------------------------------------------------------------------------|
|     1 | `ProgressReporter` ↔ real `tools/progress/progress_server.py`. Proves the C++ reporter is wire-compatible: drives 5 reports + `finish()`, then queries the server and asserts the job ended up at `progress=1.0, done=true`. |
|     2 | `HpglPlotter` + `AsyncSerialSender` ↔ `fake_plotter.py` over a `socat`-created pty pair. Runs once with model `7470A` (no memory init) and once with `7550A` (asserts `ESC.T…:` + `ESC.@…:` follow identify). Verifies identify / freeMemory / sendWait, batched send, LB-terminator handling, and queue drain. |

## Running

```sh
./tests/integration/run.sh
```

The script:
1. Detects `docker compose` (v2) or `docker-compose` (v1).
2. Builds the test image (cached after first run; ~30 s cold).
3. Starts `progress-server` and waits for it to accept TCP.
4. Runs `tester`, which compiles the project + drivers and executes both slices.
5. Exits with the tester's exit code.

Exit 0 = both slices passed.

## Layout

```
tests/integration/
├── Dockerfile               # debian + meson + libserialport + socat + python3
├── docker-compose.yml       # progress-server + tester services
├── run.sh                   # host entry point
├── run_inside.sh            # runs inside the tester container — assertions live here
├── fake_plotter.py          # Python HPGL responder (slice 2)
├── integration_reporter.cpp # C++ driver for slice 1
└── integration_plotter.cpp  # C++ driver for slice 2
```

The two C++ drivers are built by the main meson project (see [tests/meson.build](../meson.build)). They link the same static libs the production binary uses, so a regression in `src/reporter/` or `src/sender/` shows up here too.

## How slice 2 works

`socat` creates two PTYs and links them with `link=`:

```
socat -d -d pty,raw,echo=0,link=/tmp/fake_a pty,raw,echo=0,link=/tmp/fake_b
                                    │                              │
                                    ▼                              ▼
                          fake_plotter.py                integration_plotter
                          (reads commands,               (libserialport: opens
                           writes replies)                /tmp/fake_b, drives the
                                                          HpglPlotter + sender)
```

`fake_plotter.py` parses HPGL/ESC sequences and responds the way a real plotter would (CR-terminated). Every byte received is appended to `/tmp/fake_<label>.log`, and `run_inside.sh` greps that log to assert the right commands arrived.

## Adding more slices

To extend, add another `run_*` function in `run_inside.sh` and (if the new slice needs a C++ driver) another binary in `tests/meson.build`. The Dockerfile already has the deps you're likely to need (meson, gcc, libserialport-dev, glfw-dev, python3, socat, lsof).

## Troubleshooting

- **`socat: command not found`** — only matters if you run the slice outside Docker. Inside the container it's pre-installed.
- **`progress-server` never becomes healthy** — check `docker compose logs progress-server`. The healthcheck just opens a TCP socket; if that fails the server didn't bind.
- **Slice 2 timeouts** — `socat -d -d` logs go to `/tmp/socat_<label>.log` inside the container. The script prints the fake-plotter log on failure; the socat log is also useful (`docker compose exec tester cat /tmp/socat_7470A.log`).
- **First build is slow** — the imgui wrap clones from github inside the container. Subsequent runs reuse `build-integration/` under the mounted source tree.
