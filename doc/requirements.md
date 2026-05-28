# Linux Process and GPU Monitoring Dashboard Requirements

## 1. Background

This project builds a lightweight system monitoring dashboard for Linux servers. The first target environment is Ubuntu 24.x with NVIDIA GPUs. The service will run behind nginx and be exposed to the local area network through reverse proxy rules.

The backend must be implemented in C and should collect system metrics with low overhead. The frontend may use a modern web stack. The first version is read-only monitoring, but the architecture should leave clear extension points for authentication, authorization, and process control operations.

## 2. Goals

- Provide a web page for viewing per-process CPU usage and detailed process information.
- Provide a web page for viewing NVIDIA GPU usage and GPU process information.
- Use a C backend to collect metrics efficiently and expose structured API data.
- Use a modern frontend to present searchable, sortable, and auto-refreshing monitoring views.
- Support configurable sampling interval through a config file. Changes take effect after service restart.
- Keep the first version read-only.
- Reserve design space for future login authentication and process operations.

## 3. Non-Goals For Version 1

- No login page or user management.
- No in-page runtime configuration editing.
- No process operation APIs enabled by default, including kill, pause, resume, or renice.
- No first-class support for AMD or Intel GPU metrics.
- No distributed monitoring across multiple machines.
- No historical time-series storage or long-term metrics database.
- No alerting system.

## 4. Target Environment

- Operating system: Linux, primarily Ubuntu 24.x.
- CPU/process data source: Linux procfs, mainly `/proc`.
- GPU vendor: NVIDIA.
- GPU data source: NVIDIA Management Library, NVML.
- Access mode: backend service behind nginx reverse proxy.
- Default bind address: `127.0.0.1`.
- Default service port: configurable, recommended `18080`.

## 5. Users And Access Model

Version 1 assumes the dashboard is used by trusted users on the local area network. nginx is responsible for exposing the service URL and may later provide authentication at the reverse proxy layer.

The backend should still be structured so that future authentication and authorization can be added without rewriting all API handlers.

## 6. Functional Requirements

### 6.1 CPU And Process Monitoring Page

The CPU/process page must show the current state of system processes.

Required summary data:

- System CPU usage.
- Process count.
- Running process count.
- Sampling timestamp.
- Configured sampling interval.

Required process table fields:

- PID.
- PPID.
- Process name.
- Executable or command name when available.
- Full command line when available.
- User ID.
- User name when resolvable.
- Process state.
- CPU usage percentage.
- Resident memory size, RSS.
- Virtual memory size, VSZ.
- Thread count.
- Priority.
- Nice value.
- Start time.
- Running duration.
- Read bytes.
- Write bytes.

Required interactions:

- Auto-refresh from backend data.
- Sort by CPU usage, memory usage, PID, name, state, or user.
- Search by PID, name, user, or command line.
- Open a process detail view from a table row.

Process detail should include:

- All table fields.
- Raw identifiers and timing fields useful for troubleshooting.
- IO counters when available.
- Error or unavailable markers when some data cannot be read due to permissions or process exit.

### 6.2 GPU Monitoring Page

The GPU page must show NVIDIA GPU device metrics and GPU process metrics.

Required GPU fields:

- GPU index.
- GPU UUID.
- GPU name/model.
- Driver version.
- NVML version when available.
- GPU utilization percentage.
- Memory utilization percentage.
- Total memory.
- Used memory.
- Free memory.
- Temperature.
- Power usage.
- Power limit when available.
- Fan speed when available.
- Sampling timestamp.

Required GPU process fields:

- GPU index.
- GPU UUID.
- PID.
- Process name, enriched from process snapshot when possible.
- User ID.
- User name when resolvable.
- Command line when available.
- GPU memory usage.
- Process type if NVML exposes it.
- CPU usage percentage, enriched from process snapshot when possible.
- RSS and VSZ memory, enriched from process snapshot when possible.

Required interactions:

- Auto-refresh from backend data.
- Display one or more GPUs.
- Show GPU process table.
- Search or filter GPU process entries by PID, name, user, or GPU index.
- Open process detail view for GPU processes where process data is still available.

### 6.3 Configuration

Version 1 configuration is file-based. Configuration changes require restarting the backend service.

Required configuration fields:

```toml
listen_host = "127.0.0.1"
listen_port = 18080
sample_interval_ms = 1000
enable_gpu = true
enable_process_ops = false
enable_auth = false
```

Requirements:

- Default sampling interval is `1000ms`.
- The backend must validate configuration values at startup.
- Invalid configuration should fail fast with a clear error.
- Runtime configuration editing through the frontend is not required in version 1.

### 6.4 HTTP API

The backend must expose versioned HTTP APIs under `/api/v1`.

Required APIs:

```text
GET /api/v1/config
GET /api/v1/system/summary
GET /api/v1/processes
GET /api/v1/processes/{pid}
GET /api/v1/gpus
GET /api/v1/gpus/{gpu_id}
GET /api/v1/gpu-processes
```

Reserved future APIs:

```text
POST /api/v1/processes/{pid}/kill
POST /api/v1/processes/{pid}/pause
POST /api/v1/processes/{pid}/resume
POST /api/v1/processes/{pid}/renice
```

Reserved APIs may return `501 Not Implemented` in version 1 if route placeholders are added.

### 6.5 Static Frontend Serving

The backend may serve built frontend static assets directly, or nginx may serve the frontend while proxying `/api/v1/*` to the backend.

The recommended deployment model is:

- nginx serves the built frontend.
- nginx reverse proxies `/api/v1/*` to the local backend.

## 7. Performance Requirements

- API requests must read from the latest in-memory snapshot instead of triggering full process or GPU scans.
- Backend sampling should use one background sampler loop.
- Default sampling interval is 1 second.
- The sampler should avoid excessive allocations in hot paths where practical.
- The backend should tolerate short-lived processes that disappear while being inspected.
- The backend should remain usable on systems with thousands of processes.
- HTTP API responses should be generated from cached snapshots.

## 8. Reliability Requirements

- The backend must continue running if a process exits during sampling.
- Missing or permission-denied process fields should be represented as unavailable, not as fatal errors.
- GPU probing failure must not crash the service.
- If NVML is unavailable and `enable_gpu = true`, the service should expose a clear GPU probe error in API responses.
- If `enable_gpu = false`, GPU endpoints should return a disabled status or an appropriate error response.
- Snapshot timestamps must be included so the frontend can detect stale data.

## 9. Security Requirements

Version 1 does not implement authentication, but the design must account for future authentication.

Required security posture for version 1:

- Default bind host should be `127.0.0.1`.
- Process operation functionality must be disabled.
- Configuration editing API must not be exposed.
- The service should be deployed behind nginx for LAN access.
- API versioning must be used so future authentication changes can be introduced cleanly.

Future security extension points:

- Authentication middleware.
- Request context.
- Role-based permission checks.
- Audit logging for process operations.
- Optional nginx-based authentication integration.

## 10. Frontend Requirements

Recommended frontend stack:

- React.
- TypeScript.
- Vite.
- ECharts for charts.
- TanStack Table or a lightweight table implementation for sortable/filterable tables.

Required frontend views:

- CPU/process monitoring page.
- GPU monitoring page.

Required frontend behavior:

- Fetch current backend configuration from `/api/v1/config`.
- Refresh data automatically based on configured sampling interval.
- Show loading, error, stale-data, and empty states.
- Support desktop browser usage on a LAN.
- Keep the interface dense and operational, not marketing-oriented.

## 11. Deployment Requirements

Expected deployment shape:

```text
browser
  -> nginx
      -> static frontend files
      -> reverse proxy /api/v1/* to 127.0.0.1:18080
          -> C monitoring backend
```

The backend should support:

- Running as a normal process during development.
- Running under systemd in production.
- Reading a config file path from a command-line argument or default location.

## 12. Open Questions

The following items can be finalized during implementation:

- Exact frontend visual design.
- Whether frontend static files are served by nginx only or also by the C backend.
- Exact C HTTP server library choice, with Mongoose and CivetWeb as primary candidates.
- Exact JSON library choice, with yyjson as the preferred candidate.
- Exact systemd unit and packaging format.

