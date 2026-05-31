#!/usr/bin/env bash
#
# Monitor Dashboard control script
# Usage: ./ctl.sh {setup|start|stop|restart|status|dev|build|check}
#
# setup   — one-command fresh-machine setup (check + build + install + start)
# start   — build (clean) + start backend + deploy frontend to nginx
# stop    — stop backend / frontend dev server
# restart — stop + start
# status  — show health and status
# dev     — start backend + Vite dev server in foreground (Ctrl-C to stop)
# build   — build backend and frontend (no deploy)
# check   — verify all dependencies and report missing ones

set -uo pipefail
# Note: set -e NOT used because arithmetic expressions like
# errors=$((errors+1)) would trigger early exit on 0 -> 1 transitions.
# Errors are handled explicitly via return codes and the errors counter.

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
NGINX_CONF_SRC="$SCRIPT_DIR/deploy/nginx/monitor-dashboard.conf"
NGINX_CONF_DST="/etc/nginx/sites-available/monitor-dashboard.conf"
NGINX_CONF_LINK="/etc/nginx/sites-enabled/monitor-dashboard.conf"
SYSTEMD_SRC="$SCRIPT_DIR/deploy/systemd/monitor-dashboard.service"
SYSTEMD_DST="/etc/systemd/system/monitor-dashboard.service"

COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_YELLOW='\033[1;33m'
COLOR_RESET='\033[0m'

ok()  { echo -e "  ${COLOR_GREEN}[OK]${COLOR_RESET} $*"; }
warn(){ echo -e "  ${COLOR_YELLOW}[WARN]${COLOR_RESET} $*"; }
fail(){ echo -e "  ${COLOR_RED}[FAIL]${COLOR_RESET} $*"; }

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

need_sudo() {
    if sudo -n true 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

# ---- check command ----
# Verifies all build and runtime dependencies. Exits 0 if all OK, 1 if
# warnings (non-critical), 2 if errors (blocking).

cmd_check() {
    local errors=0
    local warnings=0

    echo "=============================="
    echo "  Dependency Check"
    echo "=============================="
    echo ""

    # --- Backend build tools ---
    echo "Backend build dependencies:"
    if command -v gcc &>/dev/null; then
        ok "gcc $(gcc --version | head -1 | awk '{print $NF}')"
    else
        fail "gcc not found — install build-essential or gcc"
        errors=$((errors + 1))
    fi

    if command -v make &>/dev/null; then
        ok "make $(make --version | head -1 | awk '{print $NF}')"
    else
        fail "make not found"
        errors=$((errors + 1))
    fi

    if echo '#include <pthread.h>
int main(){return 0;}' | gcc -x c - -o /dev/null 2>/dev/null; then
        ok "libpthread (via glibc)"
    else
        fail "pthread.h not usable — install build-essential"
        errors=$((errors + 1))
    fi

    echo ""

    # --- NVML (GPU) ---
    echo "GPU monitoring (NVML):"

    local nvml_h_found=""
    for p in /usr/local/cuda-*/targets/x86_64-linux/include/nvml.h \
             /usr/local/cuda/include/nvml.h \
             /usr/local/cuda/targets/x86_64-linux/include/nvml.h \
             /usr/include/nvml.h; do
        if [ -f "$p" ]; then
            nvml_h_found="$p"
            break
        fi
    done

    if [ -n "$nvml_h_found" ]; then
        ok "nvml.h: $nvml_h_found"
    else
        warn "nvml.h not found — GPU monitoring will be disabled"
        warn "  Install CUDA toolkit or nvidia-ml-dev"
        warnings=$((warnings + 1))
    fi

    local nvml_lib
    nvml_lib=$(ldconfig -p 2>/dev/null | grep 'libnvidia-ml' | head -1) || true
    if [ -n "$nvml_lib" ]; then
        local libpath
        libpath=$(echo "$nvml_lib" | sed 's/.*=> //')
        ok "libnvidia-ml: $libpath"
    else
        if [ -n "$nvml_h_found" ]; then
            warn "libnvidia-ml not found in ldconfig — NVML may fail to link"
            warn "  Install nvidia-ml-dev or CUDA toolkit"
            warnings=$((warnings + 1))
        fi
    fi

    echo ""

    # --- Frontend build tools ---
    echo "Frontend build dependencies:"
    if command -v node &>/dev/null; then
        ok "node $(node --version)"
    else
        fail "node not found — install nodejs"
        errors=$((errors + 1))
    fi

    if command -v npm &>/dev/null; then
        ok "npm $(npm --version)"
    else
        fail "npm not found"
        errors=$((errors + 1))
    fi

    echo ""

    # --- Nginx (production) ---
    echo "Production deployment:"
    if command -v nginx &>/dev/null; then
        ok "nginx $(nginx -v 2>&1 | cut -d/ -f2)"
    else
        warn "nginx not found — install nginx for production deployment"
        warn "  Or use './ctl.sh dev' for development mode"
        warnings=$((warnings + 1))
    fi

    if [ -f "$NGINX_CONF_LINK" ] || [ -f "$NGINX_CONF_DST" ]; then
        ok "nginx config already installed"
    else
        warn "nginx config not installed — run './ctl.sh setup' or:"
        warn "  sudo cp $NGINX_CONF_SRC $NGINX_CONF_DST"
        warn "  sudo ln -sf $NGINX_CONF_DST $NGINX_CONF_LINK"
        warnings=$((warnings + 1))
    fi

    echo ""

    # --- Port availability ---
    echo "Runtime:"
    if ! ss -tlnp 2>/dev/null | grep -q ':18080 '; then
        ok "port 18080 available"
    else
        warn "port 18080 is in use — backend may fail to start"
        warnings=$((warnings + 1))
    fi

    echo ""
    echo "=============================="
    if [ "$errors" -gt 0 ]; then
        echo "Result: $errors error(s), $warnings warning(s) — fix errors before building."
        return 2
    elif [ "$warnings" -gt 0 ]; then
        echo "Result: all OK, $warnings warning(s) — build may proceed with reduced features."
        return 1
    else
        echo "Result: all dependencies satisfied."
        return 0
    fi
}

# ---- build command ----

cmd_build() {
    echo "=== Building backend ==="
    make -C "$BACKEND_DIR" clean
    make -C "$BACKEND_DIR" all

    echo ""
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

# ---- start command ----
# Always does a clean build to avoid stale .o silent corruption.

cmd_start() {
    # Pre-flight check (non-blocking warnings)
    cmd_check || true

    # Clean build
    cmd_build

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
    if command -v nginx &>/dev/null; then
        if [ ! -d "$NGINX_ROOT" ]; then
            echo "Creating nginx root: $NGINX_ROOT"
            if need_sudo; then
                sudo mkdir -p "$NGINX_ROOT"
            else
                echo "  sudo mkdir -p $NGINX_ROOT"
                warn "sudo required — run './ctl.sh deploy' manually after auth"
                return 0
            fi
        fi
        echo -n "Deploying frontend..."
        if need_sudo; then
            sudo cp -r "$FRONTEND_DIR/dist/"* "$NGINX_ROOT/" && echo " done"
        else
            echo ""
            warn "sudo required for nginx deploy."
            echo "  sudo cp -r $FRONTEND_DIR/dist/* $NGINX_ROOT/"
        fi
    else
        warn "nginx not found — frontend not deployed."
        echo "  Install nginx or use './ctl.sh dev' for development mode."
        echo "  Backend API is available at http://127.0.0.1:18080/api/v1/"
    fi
}

# ---- stop command ----

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

# ---- status command ----

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
            # GPU probe status
            local gpu_status
            gpu_status=$(curl -s http://127.0.0.1:18080/api/v1/system/summary 2>/dev/null \
                | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['gpu_probe']['status'])" 2>/dev/null || echo "?")
            if [ "$gpu_status" = "ok" ]; then
                local gpu_count
                gpu_count=$(curl -s http://127.0.0.1:18080/api/v1/gpus 2>/dev/null \
                    | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d['data']['gpus']))" 2>/dev/null || echo "0")
                echo "  GPU:     $gpu_count device(s), probe OK"
            elif [ "$gpu_status" = "disabled" ]; then
                echo "  GPU:     disabled by config"
            else
                local gpu_err
                gpu_err=$(curl -s http://127.0.0.1:18080/api/v1/system/summary 2>/dev/null \
                    | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['data']['gpu_probe'].get('error_message','unknown'))" 2>/dev/null || echo "?")
                echo "  GPU:     $gpu_status — $gpu_err"
            fi
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
        echo "Nginx deploy: NOT DEPLOYED (run './ctl.sh start' or './ctl.sh setup')"
    fi
}

# ---- deploy command (frontend to nginx only) ----

cmd_deploy() {
    if [ ! -d "$FRONTEND_DIR/dist" ]; then
        echo "Frontend not built. Run: $0 build"
        return 1
    fi

    if ! command -v nginx &>/dev/null; then
        echo "nginx is not installed."
        echo "  Install: sudo apt install nginx"
        echo "  Then configure: sudo cp $NGINX_CONF_SRC $NGINX_CONF_DST"
        echo "                  sudo ln -sf $NGINX_CONF_DST $NGINX_CONF_LINK"
        echo "                  sudo systemctl reload nginx"
        return 1
    fi

    if [ ! -d "$NGINX_ROOT" ]; then
        sudo mkdir -p "$NGINX_ROOT"
    fi

    echo -n "Deploying frontend to nginx root..."
    sudo cp -r "$FRONTEND_DIR/dist/"* "$NGINX_ROOT/"
    echo " done ($NGINX_ROOT)"

    # Reload nginx if config is installed
    if [ -f "$NGINX_CONF_LINK" ] || [ -f "$NGINX_CONF_DST" ]; then
        if sudo nginx -t 2>/dev/null; then
            sudo systemctl reload nginx 2>/dev/null || sudo nginx -s reload
            echo "nginx reloaded."
        fi
    fi
}

# ---- setup command ----
# One-command fresh-machine initialization.

cmd_setup() {
    echo "=============================="
    echo "  Monitor Dashboard Setup"
    echo "=============================="
    echo ""

    # 1. Check dependencies
    echo ">>> Checking dependencies..."
    cmd_check
    local check_rc=$?
    if [ "$check_rc" -eq 2 ]; then
        echo ""
        echo "Critical dependencies missing. Install them and re-run:"
        echo "  sudo apt install build-essential gcc make nodejs npm nginx"
        echo "  # For GPU monitoring also: nvidia-driver-XXX or cuda-toolkit"
        return 1
    fi
    echo ""

    # 2. Build
    echo ">>> Building..."
    cmd_build
    echo ""

    # 3. Install nginx config
    if command -v nginx &>/dev/null; then
        echo ">>> Configuring nginx..."
        if [ ! -f "$NGINX_CONF_DST" ]; then
            sudo cp "$NGINX_CONF_SRC" "$NGINX_CONF_DST"
            echo "  Copied config to $NGINX_CONF_DST"
        else
            echo "  nginx config already exists at $NGINX_CONF_DST"
        fi
        if [ ! -L "$NGINX_CONF_LINK" ] && [ ! -f "$NGINX_CONF_LINK" ]; then
            sudo ln -sf "$NGINX_CONF_DST" "$NGINX_CONF_LINK"
            echo "  Enabled site: $NGINX_CONF_LINK"
        fi
        if [ ! -d "$NGINX_ROOT" ]; then
            sudo mkdir -p "$NGINX_ROOT"
        fi
        sudo cp -r "$FRONTEND_DIR/dist/"* "$NGINX_ROOT/"
        echo "  Frontend deployed to $NGINX_ROOT"

        if sudo nginx -t 2>/dev/null; then
            sudo systemctl reload nginx 2>/dev/null || sudo nginx -s reload
            echo "  nginx reloaded."
        else
            warn "nginx config test failed — check $NGINX_CONF_DST"
        fi
    fi
    echo ""

    # 4. Install systemd service
    if command -v systemctl &>/dev/null; then
        echo ">>> Installing systemd service..."
        if [ -f "$SYSTEMD_SRC" ]; then
            sudo cp "$SYSTEMD_SRC" "$SYSTEMD_DST"
            sudo systemctl daemon-reload
            echo "  Installed to $SYSTEMD_DST"
            echo "  Enable on boot: sudo systemctl enable monitor-dashboard"
        else
            warn "$SYSTEMD_SRC not found — skipping systemd"
        fi
    fi
    echo ""

    # 5. Start backend
    echo ">>> Starting backend..."
    cmd_start
    echo ""

    echo "=============================="
    echo "  Setup complete"
    echo "=============================="
    echo ""
    echo "  Backend API:  http://127.0.0.1:18080/api/v1/"
    if command -v nginx &>/dev/null; then
        echo "  Dashboard:    http://<host>/   (via nginx)"
    fi
    echo ""
    echo "  Manage:  ./ctl.sh {start|stop|restart|status|dev}"
    echo "  Logs:    $LOG_DIR/"
}

# ---- dev command ----

cmd_dev() {
    # Build if needed
    if [ ! -f "$BACKEND_DIR/monitor-backend" ]; then
        echo "Building backend..."
        make -C "$BACKEND_DIR" clean
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

    trap 'echo ""; echo "Shutting down..."; kill $be_pid 2>/dev/null; rm -f '"$BE_PID_FILE"'; exit 0' INT TERM

    npm run dev -- --host 0.0.0.0

    # Cleanup after dev server exits
    kill "$be_pid" 2>/dev/null || true
    rm -f "$BE_PID_FILE"
}

# ---- frontend-only management ----

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
    echo "Usage: $0 {setup|start|stop|restart|status|dev|build|check|deploy}"
    echo ""
    echo "  setup     One-command fresh-machine setup (check + build + install + start)"
    echo "  check     Verify all build/runtime dependencies"
    echo "  build     Build backend and frontend (no deploy)"
    echo "  start     Build + start backend + deploy frontend to nginx"
    echo "  stop      Stop backend / frontend dev server"
    echo "  restart   Stop then start"
    echo "  status    Show health and status"
    echo "  dev       Start backend + Vite dev server (foreground, Ctrl-C to stop)"
    echo "  deploy    Deploy frontend to nginx (requires sudo)"
    echo ""
    echo "  fe-start  Start Vite dev server in background"
    echo "  fe-stop   Stop Vite dev server"
    echo ""
    echo "Logs: $LOG_DIR/"
    exit 1
}

case "${1:-}" in
    setup)          cmd_setup ;;
    check)          cmd_check ;;
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
