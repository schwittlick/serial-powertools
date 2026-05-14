# Progress Reporter — Setup & Usage

The C++ reporter ([src/reporter/ProgressReporter.cpp](../src/reporter/ProgressReporter.cpp)) is **compiled into `serial-powertools`** — there is nothing extra to install on the plotter host. When you send a file, the app spawns a reporter that streams progress over TCP to a `progress_server` somewhere on your network.

This doc covers:
1. What you need running for progress to show up anywhere
2. How to configure the reporter inside `serial-powertools`
3. How to verify it works
4. Behavior when things go wrong

For the **server / waybar / Python-job sides**, see [`tools/progress/DEPLOYMENT.md`](../tools/progress/DEPLOYMENT.md). This doc is strictly about the C++ client embedded in `serial-powertools`.

## Topology

```
┌─────────────────┐   TCP newline-JSON   ┌──────────────────┐
│ serial-         │ ────────────────────▶│ progress_server  │
│ powertools      │   { type: report,    │ (Python, holds   │
│ (this binary)   │     id, label,       │  job state)      │
│                 │     progress, done } │                  │
└─────────────────┘                       └──────────────────┘
                                                  ▲
                                                  │  TCP query
                                                  │
                                          ┌──────────────────┐
                                          │ waybar_progress  │
                                          │ (desktop, polls  │
                                          │  every 5 s)      │
                                          └──────────────────┘
```

Server and waybar can both run on the same machine as `serial-powertools` (the all-defaults case) or be split across hosts.

## Prerequisite: a running progress server

The reporter is fail-soft — if no server is reachable, sends silently drop and the plotter keeps streaming. **You will not see progress anywhere unless `progress_server.py` is running** at the address the reporter targets.

Quickest possible setup (everything localhost):

```sh
python3 tools/progress/progress_server.py
```

Leave that running in a terminal. The reporter inside `serial-powertools` will now find it on `localhost:9876` by default.

For a persistent / multi-host setup (systemd unit, firewall, etc.), follow [`tools/progress/DEPLOYMENT.md`](../tools/progress/DEPLOYMENT.md) §1.

## Configuring the reporter

The reporter resolves its target server in this priority order (see [`ProgressReporter::resolveTarget`](../src/reporter/ProgressReporter.cpp)):

1. Constructor args passed by `AppState` (currently always empty — see "Customising" below)
2. `PROGRESS_HOST` / `PROGRESS_PORT` environment variables
3. Defaults: `localhost` / `9876`

So the normal way to point it at a non-default server is just an env var:

```sh
# Server on another host
PROGRESS_HOST=jobs.lan PROGRESS_PORT=9876 ./build/serial-powertools

# Or system-wide via systemd-user / shell rc
export PROGRESS_HOST=jobs.lan
```

Make sure the env var is set in the shell that **launches `serial-powertools`** — env vars do not propagate retroactively to running processes.

### What gets sent

Each plot file send triggers one reporter instance:

| Field      | Source                                                                                  |
|------------|-----------------------------------------------------------------------------------------|
| `type`     | always `"report"`                                                                       |
| `id`       | 8 hex chars, generated per-send by `ProgressReporter::makeJobId()`                      |
| `label`    | basename of the file being sent (e.g. `mydrawing.hpgl`)                                 |
| `progress` | clamped to `[0.0, 1.0]`, derived from `currentIndex / totalCommands` in `AsyncSerialSender` |
| `done`     | `true` only on the final send after `finish()` — flushed once even if the server was unreachable mid-job |

Send cadence: at most once per second (the reporter coalesces faster updates).

## Verifying it works

### Minimum smoke test (no plotter required)

In one terminal:

```sh
python3 tools/progress/progress_server.py
```

The server logs every received report. To prove the C++ reporter wire format is correct without launching the GUI, you can call it from the test binary:

```sh
meson test -C build progress_reporter -v
```

That test spins up a temporary TCP listener on `127.0.0.1`, drives the reporter through five updates plus `finish()`, and asserts:
- every line is JSON ending in `\n`
- every line carries the expected `id`, `label`, `type`
- the final line has `"done": true`

If `test_progress_reporter` passes, your build's reporter is wire-compatible with `progress_server.py`.

### End-to-end with the GUI

1. Run `python3 tools/progress/progress_server.py` (or point `serial-powertools` at a remote one via `PROGRESS_HOST`).
2. Launch `./build/serial-powertools`, connect to a plotter, **Select file**, **Send**.
3. Watch the server's stdout — you should see one `Job <id>: NN.N% done=False` line per second, then a final `done=True`.
4. Run `tools/progress/waybar_progress.py` once on the command line to confirm it sees the job:
   ```sh
   PROGRESS_HOST=<server-host> python3 tools/progress/waybar_progress.py
   ```
   The output is the one-line JSON waybar would consume. To make it live on your bar, follow [`DEPLOYMENT.md`](../tools/progress/DEPLOYMENT.md) §2.

### Wire-level cross-check against the Python reporter

The plan calls for byte-identical traffic with `tools/progress/progress_reporter.py`. To verify:

```sh
sudo tcpdump -i lo -A 'tcp port 9876'
```

Then run a send from `serial-powertools` and, in another terminal, a send from the reference Python:

```sh
python3 -c "
from tools.progress.progress_reporter import ProgressReporter
import time
r = ProgressReporter(label='compare', job_id='deadbeef').start()
for i in range(5):
    time.sleep(0.3); r.report((i+1)/5)
r.finish()
"
```

The JSON bodies should match line-for-line (ignoring `id` if you let the C++ side generate one).

## Behavior under failures

| Situation                                | What the reporter does                                                                  |
|------------------------------------------|-----------------------------------------------------------------------------------------|
| Server unreachable on send               | Drops the line silently; never blocks the plotter pipeline. Retries on the next tick.   |
| Server appears mid-job                   | Picks up on the very next update — no reconnect logic needed (new TCP conn per send).   |
| Server unreachable at `finish()`         | The final `done=true` line is still attempted; if it also fails the job will look "running" on the server until cleared. |
| `serial-powertools` killed mid-job       | Same as above — the server holds the last-seen progress until you `clear` it manually or via the waybar click-to-clear. |
| DNS fails / `PROGRESS_HOST` bogus        | Silently drops, like server-unreachable. No log noise.                                  |

The reporter does **not** log to stderr on send failures. If you suspect it's not reaching the server, run with `strace -f -e trace=connect ./build/serial-powertools` or `tcpdump` to confirm whether connection attempts are happening.

## Customising

The reporter is currently constructed inside [`AppState::sendFile`](../src/app/AppState.cpp) with only the file label. If you want per-job overrides (fixed `job_id`, alternate host/port, faster `min_interval`), the constructor already supports them:

```cpp
ProgressReporter(label, jobId, host, port, minIntervalSec);
```

Pass empty `host` / `0` port to fall back to env-var / default resolution. A stable `jobId` is useful when one logical job spans several runs of the binary — pass the same id and the server overwrites the same row instead of accumulating new ones.
