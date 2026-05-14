#!/usr/bin/env bash
# Runs INSIDE the `tester` container. Builds the project, runs unit tests,
# then drives the two integration slices and asserts.
#
# Exits non-zero on the first failure.

set -euo pipefail

cd /workspace

# ── Build ─────────────────────────────────────────────────────────────────
BUILD_DIR="build-integration"
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "── meson setup ──"
    meson setup "$BUILD_DIR"
fi
echo "── meson compile ──"
meson compile -C "$BUILD_DIR"

echo "── unit tests ──"
meson test -C "$BUILD_DIR" --print-errorlogs

REPORTER_BIN="$BUILD_DIR/tests/integration_reporter"
PLOTTER_BIN="$BUILD_DIR/tests/integration_plotter"

for bin in "$REPORTER_BIN" "$PLOTTER_BIN"; do
    if [ ! -x "$bin" ]; then
        echo "error: $bin missing — did meson build it?" >&2
        exit 2
    fi
done

# ── Slice 1: ProgressReporter ↔ progress_server.py ────────────────────────
echo
echo "════ Slice 1: ProgressReporter ↔ progress_server ════"

JOB_ID="deadbeef"
JOB_LABEL="integration-job-A"

"$REPORTER_BIN" "$JOB_LABEL" "$JOB_ID"

# Wait briefly for the server to absorb the final done=true line.
sleep 0.5

python3 - "$JOB_ID" "$JOB_LABEL" <<'PY'
import json, os, socket, sys
job_id, expected_label = sys.argv[1], sys.argv[2]
host = os.environ.get("PROGRESS_HOST", "localhost")
port = int(os.environ.get("PROGRESS_PORT", "9876"))

with socket.create_connection((host, port), timeout=3) as s:
    s.sendall(b'{"type":"query"}\n')
    data = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk

resp = json.loads(data.decode())
jobs = resp.get("jobs", [])
match = [j for j in jobs if j.get("id") == job_id]
if not match:
    print(f"FAIL: job {job_id} not found in server state", file=sys.stderr)
    print(json.dumps(resp, indent=2), file=sys.stderr)
    sys.exit(1)
job = match[0]

errs = []
if not job.get("done"):
    errs.append(f"done != true: {job!r}")
if abs(job.get("progress", 0.0) - 1.0) > 0.01:
    errs.append(f"progress != 1.0: {job.get('progress')}")
if job.get("label") != expected_label:
    errs.append(f"label mismatch: got {job.get('label')!r}, want {expected_label!r}")

if errs:
    for e in errs:
        print(f"FAIL: {e}", file=sys.stderr)
    sys.exit(1)

print(f"SLICE 1 OK — job {job_id}: label={job['label']!r}, progress={job['progress']}, done={job['done']}")
PY


# ── Slice 2: HpglPlotter ↔ fake plotter (over socat pty pair) ─────────────
run_plotter_slice() {
    local label=$1 model=$2
    echo
    echo "════ Slice 2 ($label): plotter ↔ fake plotter model=$model ════"

    local sock_a="/tmp/fake_${label}_a"
    local sock_b="/tmp/fake_${label}_b"
    local plot_log="/tmp/fake_${label}.log"
    rm -f "$sock_a" "$sock_b" "$plot_log"

    # socat creates two PTYs and pipes the two together. The symlinks live at
    # $sock_a / $sock_b for the fake plotter and the C++ driver to open.
    socat -d -d pty,raw,echo=0,link="$sock_a" pty,raw,echo=0,link="$sock_b" \
        > "/tmp/socat_${label}.log" 2>&1 &
    local socat_pid=$!

    # Wait until both link symlinks materialise.
    local i=0
    while [ ! -e "$sock_a" ] || [ ! -e "$sock_b" ]; do
        sleep 0.05
        i=$((i+1))
        if [ $i -gt 60 ]; then
            echo "FAIL: socat pty pair did not appear" >&2
            cat "/tmp/socat_${label}.log" >&2 || true
            kill "$socat_pid" 2>/dev/null || true
            return 1
        fi
    done

    python3 /workspace/tests/integration/fake_plotter.py \
        --tty "$sock_a" --model "$model" --log "$plot_log" &
    local fake_pid=$!

    # Give the fake a moment to open its end.
    sleep 0.3

    local plotter_rc=0
    "$PLOTTER_BIN" "$sock_b" || plotter_rc=$?

    # Tear down children.
    kill "$fake_pid"  2>/dev/null || true
    kill "$socat_pid" 2>/dev/null || true
    wait "$fake_pid"  2>/dev/null || true
    wait "$socat_pid" 2>/dev/null || true

    echo "── fake_plotter log ($plot_log) ──"
    cat "$plot_log"
    echo "── end log ──"

    if [ "$plotter_rc" -ne 0 ]; then
        echo "FAIL: integration_plotter exited $plotter_rc" >&2
        return 1
    fi

    # Required commands (regex match against the log).
    local must=(
        "ESC\\.A"                    # identify
        "ESC\\.B"                    # freeMemory
        "ESC\\.L"                    # sendWait
        "CMD .*: IN"                 # batched HPGL command
        "CMD .*: PU"
        "CMD .*: PA0,0"
        "CMD .*: PD"
        "CMD \\(LB\\): LBhello"      # LB token ended with LB_TERM (\x03)
    )
    for pat in "${must[@]}"; do
        if ! grep -E -q "$pat" "$plot_log"; then
            echo "FAIL: pattern not found in plot log: $pat" >&2
            return 1
        fi
    done

    if [ "$model" = "7550A" ]; then
        # 7550A identify triggers ESC.T<...>: and ESC.@<...>:
        if ! grep -E -q "ESC\\.T[0-9;]+:" "$plot_log"; then
            echo "FAIL: 7550A did not receive ESC.T memory config" >&2
            return 1
        fi
        if ! grep -E -q "ESC\\.@[0-9]+:" "$plot_log"; then
            echo "FAIL: 7550A did not receive ESC.@ logical buffer config" >&2
            return 1
        fi
    fi

    echo "SLICE 2 ($label) OK"
}

if [ "${INTEGRATION_SKIP_PLOTTER:-0}" = "1" ]; then
    echo
    echo "════ Slice 2 (plotter ↔ fake plotter) SKIPPED ════"
    echo "INTEGRATION_SKIP_PLOTTER=1 is set."
    echo "TODO: re-enable once SerialIo is migrated off libserialport — the"
    echo "current libserialport (0.1.x) refuses /dev/pts/* paths because it"
    echo "can't determine the transport type from sysfs for PTYs."
else
    run_plotter_slice "7470A" "7470A"
    run_plotter_slice "7550A" "7550A"
fi

echo
echo "════ All integration slices passed ════"
