# Progress System — Deployment

This directory contains the three Python pieces of the progress system. They are deployed and run **as Python** (the C++ port in this project speaks the same wire protocol but is *not* a substitute for these files).

The system is fully host-agnostic — no hostname is hardcoded. Configure via env vars or CLI flags.

## Components

| File | Where it runs | Role |
|---|---|---|
| `progress_server.py` | The job host (any machine reachable from clients) | TCP server. Holds in-memory state of all active jobs. |
| `progress_reporter.py` | Anywhere a Python job runs progress through | Client library. `import` and call from your jobs. |
| `waybar_progress.py` | The desktop running waybar | Polled by waybar every few seconds. Queries server, renders progress bars, fires `notify-send` on completion. |

Server, reporter, and waybar consumer can all be on the same machine (use defaults), or split across hosts.

## Configuration

All three components read the same env vars; CLI flags / constructor args override env vars.

| Env var | Used by | Default | Meaning |
|---|---|---|---|
| `PROGRESS_HOST` | reporter, waybar | `localhost` | Server hostname/IP that clients connect to |
| `PROGRESS_PORT` | reporter, waybar, server | `9876` | TCP port |
| `PROGRESS_BIND_HOST` | server | `0.0.0.0` | Interface the server binds to |

## Wire protocol

TCP, newline-delimited JSON to `<server>:<port>`. Reporters open a connection per send (`connect → sendall(line) → close`).

```json
{"type":"report","id":"<8-hex>","label":"<job name>","progress":0.42,"done":false}
{"type":"clear","id":"<8-hex>"}
{"type":"clear_done"}
{"type":"query"}                         // response: {"jobs":[...]}\n then server closes
{"type":"ack_notify","id":"<8-hex>"}     // waybar tells server it dispatched the notification
```

The server is authoritative — see `progress_server.py` for the exact state shape returned by `query`.

## 1. Deploy the server

`install-progress-server.sh` copies `progress_server.py` to a stable path, writes
the systemd unit, and enables + starts the service:

```sh
./install-progress-server.sh                       # per-user service, defaults
./install-progress-server.sh --port 9999            # override the port
sudo ./install-progress-server.sh --system          # system-wide service
./install-progress-server.sh --uninstall            # remove it
```

Run `./install-progress-server.sh --help` for all options.

Multi-machine: open TCP port `9876` (or your chosen `PROGRESS_PORT`) in the
server host's firewall.

Control the running service (drop `--user` if installed with `--system`):
```sh
systemctl --user status progress-server         # check state
systemctl --user restart progress-server        # restart
systemctl --user stop progress-server           # stop until next login/reboot
systemctl --user disable --now progress-server  # stop and stop it coming back
journalctl --user -u progress-server -f         # tail logs
```

## 2. Deploy the waybar consumer

Copy or symlink `waybar_progress.py` to an executable path on the desktop, e.g. `~/.local/bin/waybar_progress.py`.

Waybar config — add to `modules-right` (or wherever) in `~/.config/waybar/config[.jsonc]`. Pick whatever module name you like; the example uses `custom/job-progress`:

```jsonc
"custom/job-progress": {
    "exec": "~/.local/bin/waybar_progress.py --host <SERVER_HOST>",
    "return-type": "json",
    "interval": 5,
    "on-click": "~/.local/bin/waybar_progress.py --host <SERVER_HOST> --clear-done",
    "tooltip": true
}
```

Drop `--host …` if the server is on the same machine (default `localhost`). Or set `PROGRESS_HOST` in the waybar process environment and drop the flag everywhere.

Waybar CSS — in `~/.config/waybar/style.css` (match the class names emitted by `waybar_progress.py`: `running`, `done`, `offline`, `idle`):

```css
#custom-job-progress {
    font-family: monospace;
    padding: 0 8px;
}
#custom-job-progress.running { color: #a6e3a1; }   /* green */
#custom-job-progress.done    { color: #f9e2af; }   /* yellow — waiting for click-to-clear */
#custom-job-progress.offline { color: #f38ba8; }   /* red */
#custom-job-progress.idle    { color: #6c7086; }   /* hide when nothing is happening */
```

Reload waybar (`killall -SIGUSR2 waybar` or restart it).

What the waybar module does:
- Polls the server every 5 s, renders running jobs as mini bars and finished jobs as a `✓ N` count.
- Fires `notify-send` exactly once per newly-completed job (state cached at `~/.cache/waybar_progress_notified.json`).
- Click-to-clear → re-invokes itself with `--clear-done`, sends `{"type":"clear_done"}`, resets the local notify cache.

## 3. Use the reporter from a Python job

```python
from progress_reporter import ProgressReporter

# Defaults: host=localhost, port=9876. Override via PROGRESS_HOST/PROGRESS_PORT env vars
# or pass host=... / port=... to the constructor.
reporter = ProgressReporter(label="Render pass 1")
reporter.start()

items = load_items()
for i, item in enumerate(items):
    do_work(item)
    reporter.report((i + 1) / len(items))

reporter.finish()
```

- `start()` launches the background thread and pushes an initial `0.0` report.
- `report(progress)` is non-blocking — updates are coalesced and pushed at most once per `min_interval` (default 1 s).
- `finish()` flushes a final `done=true` report (waits up to 5 s) and joins the thread.
- Pass `job_id="my-fixed-id"` to the constructor for a stable id across runs.

The reporter is fail-soft: if the server is unreachable, updates are dropped silently and retried on the next interval. The host job never blocks on the network.

## 4. C++ integration

The C++ app (this project's `src/reporter/ProgressReporter.{h,cpp}`) speaks the same TCP+JSON protocol against the same server. No subprocess, no shared state — they're independent clients of `progress_server.py`. Use `tcpdump -i any -A 'tcp port 9876'` (adjust port to your `PROGRESS_PORT`) to cross-check that the C++ and Python clients send byte-equivalent lines.

## Manual commands (from a terminal anywhere with TCP reach)

Substitute `$HOST` / `$PORT` for your server and port.

Clear one job:
```sh
python3 -c "
import socket, json, os
host, port = os.environ.get('PROGRESS_HOST', 'localhost'), int(os.environ.get('PROGRESS_PORT', '9876'))
with socket.create_connection((host, port)) as s:
    s.sendall((json.dumps({'type':'clear','id':'JOB_ID_HERE'})+'\n').encode())
"
```

Clear all finished jobs (same as clicking the waybar module):
```sh
~/.local/bin/waybar_progress.py --host $HOST --clear-done
```

Query current state:
```sh
python3 -c "
import socket, json, os
host, port = os.environ.get('PROGRESS_HOST', 'localhost'), int(os.environ.get('PROGRESS_PORT', '9876'))
with socket.create_connection((host, port)) as s:
    s.sendall(b'{\"type\":\"query\"}\n')
    print(s.recv(65536).decode())
"
```
