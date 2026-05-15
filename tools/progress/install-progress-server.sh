#!/usr/bin/env bash
#
# install-progress-server.sh — install progress_server.py as a systemd service.
#
# Defaults to a per-user service (`systemctl --user`). Pass --system to install
# system-wide (requires root). Configuration is baked into the unit as
# Environment= lines so the service runs the same regardless of shell env.
#
# Usage:
#   ./install-progress-server.sh [options]
#
# Options:
#   --system            Install as a system-wide service (needs root).
#   --port PORT         PROGRESS_PORT for the service       (default: 9876)
#   --bind-host HOST    PROGRESS_BIND_HOST for the service  (default: 0.0.0.0)
#   --prefix DIR        Where to copy progress_server.py
#                       (default: ~/.local/bin, or /usr/local/bin for --system)
#   --uninstall         Stop, disable, and remove the service and installed script.
#   -h, --help          Show this help.

set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_SCRIPT="$SRC_DIR/progress_server.py"

# --- defaults ---------------------------------------------------------------
MODE="user"
PORT="9876"
BIND_HOST="0.0.0.0"
PREFIX=""
UNINSTALL=0

usage() { sed -n '3,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

# --- parse args -------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system)    MODE="system"; shift ;;
        --port)      PORT="$2"; shift 2 ;;
        --bind-host) BIND_HOST="$2"; shift 2 ;;
        --prefix)    PREFIX="$2"; shift 2 ;;
        --uninstall) UNINSTALL=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

# --- resolve mode-dependent paths -------------------------------------------
if [[ "$MODE" == "system" ]]; then
    if [[ $EUID -ne 0 ]]; then
        echo "error: --system requires root (re-run with sudo)." >&2
        exit 1
    fi
    PREFIX="${PREFIX:-/usr/local/bin}"
    UNIT_DIR="/etc/systemd/system"
    SYSTEMCTL=(systemctl)
    JOURNAL_HINT="journalctl -u progress-server -f"
else
    PREFIX="${PREFIX:-$HOME/.local/bin}"
    UNIT_DIR="$HOME/.config/systemd/user"
    SYSTEMCTL=(systemctl --user)
    JOURNAL_HINT="journalctl --user -u progress-server -f"
fi

INSTALLED_SCRIPT="$PREFIX/progress_server.py"
UNIT_FILE="$UNIT_DIR/progress-server.service"

# --- uninstall --------------------------------------------------------------
if [[ "$UNINSTALL" -eq 1 ]]; then
    echo "Uninstalling progress-server ($MODE service)..."
    "${SYSTEMCTL[@]}" disable --now progress-server 2>/dev/null || true
    rm -fv "$UNIT_FILE"
    rm -fv "$INSTALLED_SCRIPT"
    "${SYSTEMCTL[@]}" daemon-reload
    echo "Done."
    exit 0
fi

# --- install ----------------------------------------------------------------
if [[ ! -f "$SRC_SCRIPT" ]]; then
    echo "error: $SRC_SCRIPT not found." >&2
    exit 1
fi

PYTHON="$(command -v python3 || true)"
if [[ -z "$PYTHON" ]]; then
    echo "error: python3 not found on PATH." >&2
    exit 1
fi

echo "Installing progress-server ($MODE service)"
echo "  script : $INSTALLED_SCRIPT"
echo "  unit   : $UNIT_FILE"
echo "  port   : $PORT"
echo "  bind   : $BIND_HOST"

install -Dm755 "$SRC_SCRIPT" "$INSTALLED_SCRIPT"
mkdir -p "$UNIT_DIR"

cat > "$UNIT_FILE" <<EOF
[Unit]
Description=Job Progress Server
After=network.target

[Service]
Environment=PROGRESS_PORT=$PORT
Environment=PROGRESS_BIND_HOST=$BIND_HOST
ExecStart=$PYTHON $INSTALLED_SCRIPT
Restart=on-failure
RestartSec=5

[Install]
WantedBy=$([[ "$MODE" == "system" ]] && echo multi-user.target || echo default.target)
EOF

"${SYSTEMCTL[@]}" daemon-reload
"${SYSTEMCTL[@]}" enable --now progress-server

echo
echo "progress-server is running."
"${SYSTEMCTL[@]}" --no-pager status progress-server || true
echo
echo "Tail logs with:  $JOURNAL_HINT"
