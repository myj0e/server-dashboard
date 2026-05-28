#!/usr/bin/env bash
#
# Monitor Dashboard control script
# Usage: ./ctl.sh {start|stop|restart|status|dev|build}
#
# Production mode (start/stop/restart/status):
#   Manages C backend + builds & deploys frontend to nginx.
#
# Dev mode (dev):
#   Starts both the C backend and the Vite dev server in the foreground.
#
# Build only:
#   ./ctl.sh build    Build both backend and frontend (no deploy).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
FRONTEND_DIR="$SCRIPT_DIR/frontend"
CONFIG_FILE="$BACKEND_DIR/config/monitor.conf"
BE_PID_FILE="$SCRIPT_DIR/.monitor-backend.pid"
FE_PID_FILE="$SCRIPT_DIR/.monitor-frontend.pid"
LOG_DIR="$SCRIPT_DIR/logs"
BE_LOG="$LOG_DIR/backend.log"
FE_LOG="$LOG_DIR/frontend.log"
NGINX_ROOT="/usr/share/monitor-dashboard/public"

mkdir -p "$LOG_DIR"

# ---- helpers ----

is_running() {
    local pid_file="$1"
    if [ -f "$pid_file" ]; then
        local pid
        pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

get_pid() {
    if [ -f "$1" ]; then
        cat "$1"
    fi
}

# ---- commands ----

cmd_build() {
    echo "=== Building backend ==="
    make -C "$BACKEND_DIR" all

    echo "=== Building frontend ==="
    cd "$FRONTEND_DIR"
    if [ ! -d "node_modules" ]; then
        npm install
    fi
    npm run build
    echo ""
    echo "Build complete."
    echo "  Backend:  $BACKEND_DIR/monitor-backend"
    echo "  Frontend: $FRONTEND_DIR/dist/"
}

cmd_deploy() {
    if [ ! -d "$FRONTEND_DIR/dist" ]; then
        echo "Frontend not built. Run: $0 build"
        return 1
    fi
    echo -n "Deploying frontend to nginx root..."
    sudo cp -r "$FRONTEND_DIR/dist/"* "$NGINX_ROOT/"
    echo " done ($NGINX_ROOT)"
}

cmd_start() {
    # Build if needed
    if [ ! -f "$BACKEND_DIR/monitor-backend" ]; then
        echo "Building backend..."
        make -C "$BACKEND_DIR" all
    fi
    if [ ! -d "$FRONTEND_DIR/dist" ]; then
        echo "Building frontend..."
        cd "$FRONTEND_DIR"
        [ -d "node_modules" ] || npm install
        npm run build
    fi

    # Start backend
    if is_running "$BE_PID_FILE"; then
        echo "Backend is already running (PID $(get_pid "$BE_PID_FILE"))"
    else
        echo -n "Starting backend..."
        nohup "$BACKEND_DIR/monitor-backend" --config "$CONFIG_FILE" \
            >> "$BE_LOG" 2>&1 &
        local pid=$!
        echo "$pid" > "$BE_PID_FILE"
        sleep 1
        if kill -0 "$pid" 2>/dev/null; then
            echo " done (PID $pid)"
        else
            echo " FAILED"
            echo "  Check logs: tail -f $BE_LOG"
            rm -f "$BE_PID_FILE"
            return 1
        fi
    fi

    # Deploy frontend to nginx
    echo -n "Deploying frontend..."
    sudo cp -r "$FRONTEND_DIR/dist/"* "$NGINX_ROOT/" 2>/dev/null && echo " done" || {
        echo ""
        echo "  WARNING: frontend deploy failed (sudo required)."
        echo "  Run manually: sudo cp -r frontend/dist/* $NGINX_ROOT/"
    }
}

cmd_stop() {
    # Stop backend
    if is_running "$BE_PID_FILE"; then
        local pid
        pid=$(get_pid "$BE_PID_FILE")
        echo -n "Stopping backend (PID $pid)..."
        kill "$pid" 2>/dev/null || true
        for _ in $(seq 1 20); do
            if ! kill -0 "$pid" 2>/dev/null; then
                echo " done"
                rm -f "$BE_PID_FILE"
                break
            fi
            sleep 0.5
        done
        if kill -0 "$pid" 2>/dev/null; then
            echo -n " force killing..."
            kill -9 "$pid" 2>/dev/null || true
            sleep 1
            rm -f "$BE_PID_FILE"
            echo " done"
        fi
    else
        echo "Backend: not running"
        rm -f "$BE_PID_FILE"
    fi

    # Stop frontend dev server (if any)
    if is_running "$FE_PID_FILE"; then
        local pid
        pid=$(get_pid "$FE_PID_FILE")
        echo -n "Stopping frontend dev server (PID $pid)..."
        kill "$pid" 2>/dev/null || true
        sleep 1
        kill -9 "$pid" 2>/dev/null || true
        rm -f "$FE_PID_FILE"
        echo " done"
    fi
}

cmd_restart() {
    cmd_stop
    sleep 1
    cmd_start
}

cmd_status() {
    echo "=============================="
    echo "  Monitor Dashboard Status"
    echo "=============================="
    echo ""

    # Backend
    if is_running "$BE_PID_FILE"; then
        local pid
        pid=$(get_pid "$BE_PID_FILE")
        echo "Backend:  RUNNING (PID $pid)"
        echo "  Config: $CONFIG_FILE"
        echo "  Log:    $BE_LOG"
        echo "  API:    http://127.0.0.1:18080/api/v1/"
        if command -v curl &>/dev/null; then
            local resp
            resp=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:18080/api/v1/config 2>/dev/null || true)
            echo "  Health: HTTP $resp"
        fi
    else
        echo "Backend:  STOPPED"
    fi

    echo ""

    # Frontend dev server
    if is_running "$FE_PID_FILE"; then
        local pid
        pid=$(get_pid "$FE_PID_FILE")
        echo "Frontend (dev): RUNNING (PID $pid)"
        echo "  Log: $FE_LOG"
        echo "  URL: http://localhost:5173"
    else
        echo "Frontend (dev): STOPPED"
    fi

    # Frontend build
    if [ -d "$FRONTEND_DIR/dist" ]; then
        local ts
        ts=$(stat -c %y "$FRONTEND_DIR/dist/index.html" 2>/dev/null | cut -d. -f1 || echo "unknown")
        echo "Frontend (build): $FRONTEND_DIR/dist/  ($ts)"
    else
        echo "Frontend (build): not built"
    fi

    # Nginx deployment
    if [ -f "$NGINX_ROOT/index.html" ]; then
        local ts
        ts=$(stat -c %y "$NGINX_ROOT/index.html" 2>/dev/null | cut -d. -f1 || echo "unknown")
        echo "Nginx deploy: $NGINX_ROOT/  ($ts)"
    else
        echo "Nginx deploy: NOT DEPLOYED (run: $0 start)"
    fi
}

cmd_dev() {
    # Build if needed
    if [ ! -f "$BACKEND_DIR/monitor-backend" ]; then
        echo "Building backend..."
        make -C "$BACKEND_DIR" all
    fi

    # Stop any existing instances
    cmd_stop 2>/dev/null || true

    echo "Starting in development mode..."
    echo ""

    # Start backend in background
    echo -n "Starting backend..."
    nohup "$BACKEND_DIR/monitor-backend" --config "$CONFIG_FILE" \
        >> "$BE_LOG" 2>&1 &
    local be_pid=$!
    echo "$be_pid" > "$BE_PID_FILE"
    echo " done (PID $be_pid)"
    echo "  API: http://127.0.0.1:18080/api/v1/"
    echo ""

    # Start frontend dev server in foreground
    echo "Starting frontend dev server..."
    cd "$FRONTEND_DIR"
    if [ ! -d "node_modules" ]; then
        npm install
    fi

    # Trap to clean up backend on exit
    trap 'echo ""; echo "Shutting down..."; kill $be_pid 2>/dev/null; rm -f '"$BE_PID_FILE"'; exit 0' INT TERM

    npm run dev -- --host 0.0.0.0

    # Cleanup after dev server exits
    kill "$be_pid" 2>/dev/null || true
    rm -f "$BE_PID_FILE"
}

cmd_frontend_start() {
    if is_running "$FE_PID_FILE"; then
        echo "Frontend dev server is already running (PID $(get_pid "$FE_PID_FILE"))"
        return 1
    fi

    cd "$FRONTEND_DIR"
    if [ ! -d "node_modules" ]; then
        npm install
    fi

    echo -n "Starting frontend dev server..."
    nohup npm run dev -- --host 0.0.0.0 >> "$FE_LOG" 2>&1 &
    local pid=$!
    echo "$pid" > "$FE_PID_FILE"
    sleep 2
    if kill -0 "$pid" 2>/dev/null; then
        echo " done (PID $pid)"
        echo "  URL: http://localhost:5173"
    else
        echo " FAILED"
        echo "  Check logs: tail -f $FE_LOG"
        rm -f "$FE_PID_FILE"
        return 1
    fi
}

cmd_frontend_stop() {
    if ! is_running "$FE_PID_FILE"; then
        echo "Frontend dev server is not running"
        rm -f "$FE_PID_FILE"
        return 0
    fi

    local pid
    pid=$(get_pid "$FE_PID_FILE")
    echo -n "Stopping frontend dev server (PID $pid)..."
    kill "$pid" 2>/dev/null || true
    sleep 1
    kill -9 "$pid" 2>/dev/null || true
    rm -f "$FE_PID_FILE"
    echo " done"
}

# ---- main ----

usage() {
    echo "Usage: $0 {start|stop|restart|status|dev|build}"
    echo ""
    echo "  start     Build & start backend + deploy frontend to nginx"
    echo "  stop      Stop backend"
    echo "  restart   Restart backend"
    echo "  status    Show health and status"
    echo "  dev       Start backend + Vite dev server (foreground, Ctrl-C to stop)"
    echo "  build     Build both backend and frontend (no deploy)"
    echo ""
    echo "  fe-start  Start Vite dev server in background"
    echo "  fe-stop   Stop Vite dev server"
    echo ""
    echo "Logs: $LOG_DIR/"
    exit 1
}

case "${1:-}" in
    start)          cmd_start ;;
    stop)           cmd_stop ;;
    restart)        cmd_restart ;;
    status)         cmd_status ;;
    dev)            cmd_dev ;;
    build)          cmd_build ;;
    deploy)         cmd_deploy ;;
    fe-start)       cmd_frontend_start ;;
    fe-stop)        cmd_frontend_stop ;;
    *)              usage ;;
esac
