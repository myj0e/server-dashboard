#!/usr/bin/env bash
#
# Monitor Dashboard control script
# Usage: ./ctl.sh {start|stop|restart|status|dev}
#
# Production mode (start/stop/restart/status):
#   Manages only the C backend process. Frontend is served by nginx.
#
# Dev mode (dev):
#   Starts both the C backend and the Vite dev server in the foreground.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
CONFIG_FILE="$BACKEND_DIR/config/monitor.conf"
PID_FILE="$SCRIPT_DIR/.monitor-backend.pid"
LOG_FILE="$SCRIPT_DIR/.monitor-backend.log"

# ---- helpers ----

is_running() {
    if [ -f "$PID_FILE" ]; then
        local pid
        pid=$(cat "$PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

get_pid() {
    if [ -f "$PID_FILE" ]; then
        cat "$PID_FILE"
    fi
}

# ---- commands ----

cmd_start() {
    if is_running; then
        echo "Backend is already running (PID $(get_pid))"
        return 1
    fi

    if [ ! -f "$BACKEND_DIR/monitor-backend" ]; then
        echo "Building backend..."
        make -C "$BACKEND_DIR" all
    fi

    echo -n "Starting backend..."
    nohup "$BACKEND_DIR/monitor-backend" --config "$CONFIG_FILE" \
        >> "$LOG_FILE" 2>&1 &
    local pid=$!
    echo "$pid" > "$PID_FILE"

    # Wait a moment to check if it stays up
    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
        echo " done (PID $pid)"
        echo "  API: http://127.0.0.1:18080/api/v1/"
    else
        echo " FAILED"
        echo "Check logs: tail -f $LOG_FILE"
        rm -f "$PID_FILE"
        return 1
    fi
}

cmd_stop() {
    if ! is_running; then
        echo "Backend is not running"
        rm -f "$PID_FILE"
        return 0
    fi

    local pid
    pid=$(get_pid)
    echo -n "Stopping backend (PID $pid)..."
    kill "$pid" 2>/dev/null || true

    # Wait for graceful shutdown (up to 10s)
    for _ in $(seq 1 20); do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo " done"
            rm -f "$PID_FILE"
            return 0
        fi
        sleep 0.5
    done

    # Force kill if still running
    echo -n " force killing..."
    kill -9 "$pid" 2>/dev/null || true
    sleep 1
    rm -f "$PID_FILE"
    echo " done"
}

cmd_restart() {
    cmd_stop
    sleep 1
    cmd_start
}

cmd_status() {
    if is_running; then
        local pid
        pid=$(get_pid)
        echo "Backend: RUNNING (PID $pid)"
        echo "  Log:    $LOG_FILE"
        echo "  Config: $CONFIG_FILE"
        echo "  API:    http://127.0.0.1:18080/api/v1/config"

        # Quick health check
        if command -v curl &>/dev/null; then
            local resp
            resp=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:18080/api/v1/config 2>/dev/null || true)
            echo "  Health: HTTP $resp"
        fi
    else
        echo "Backend: STOPPED"
    fi

    # Show frontend status
    if [ -d "$SCRIPT_DIR/frontend/dist" ]; then
        echo "Frontend: built (dist/)"
    else
        echo "Frontend: not built (run 'cd frontend && npm run build')"
    fi
}

cmd_dev() {
    echo "Starting in development mode..."

    # Build backend if needed
    if [ ! -f "$BACKEND_DIR/monitor-backend" ]; then
        echo "Building backend..."
        make -C "$BACKEND_DIR" all
    fi

    # Start backend in background
    echo -n "Starting backend..."
    "$BACKEND_DIR/monitor-backend" --config "$CONFIG_FILE" \
        >> "$LOG_FILE" 2>&1 &
    local be_pid=$!
    echo "$be_pid" > "$PID_FILE"
    echo " done (PID $be_pid)"

    # Start frontend dev server in foreground
    echo "Starting frontend dev server..."
    cd "$SCRIPT_DIR/frontend"
    if [ ! -d "node_modules" ]; then
        npm install
    fi

    # Trap to clean up backend on exit
    trap 'echo "Shutting down..."; kill $be_pid 2>/dev/null; rm -f '"$PID_FILE"'; exit 0' INT TERM

    npm run dev

    # Cleanup after dev server exits
    kill "$be_pid" 2>/dev/null || true
    rm -f "$PID_FILE"
}

# ---- main ----

usage() {
    echo "Usage: $0 {start|stop|restart|status|dev}"
    echo ""
    echo "  start   - Start the C backend as a daemon"
    echo "  stop    - Stop the backend"
    echo "  restart - Restart the backend"
    echo "  status  - Show health and status"
    echo "  dev     - Start backend + Vite dev server (foreground, Ctrl-C to stop)"
    exit 1
}

case "${1:-}" in
    start)   cmd_start ;;
    stop)    cmd_stop ;;
    restart) cmd_restart ;;
    status)  cmd_status ;;
    dev)     cmd_dev ;;
    *)       usage ;;
esac
