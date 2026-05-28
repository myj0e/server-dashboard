# Linux Process and GPU Monitoring Dashboard Design

## 1. Overview

The system is a lightweight monitoring dashboard for Ubuntu 24.x servers with NVIDIA GPUs. It uses a C backend to collect process, CPU, and GPU data, and a modern web frontend to display the latest snapshot.

The backend follows a snapshot-based design:

1. A sampler thread periodically collects process and GPU metrics.
2. The sampler publishes a complete immutable snapshot.
3. HTTP handlers serialize the latest snapshot into JSON.
4. The frontend polls the APIs at the configured interval.

This avoids running expensive system scans directly inside HTTP request handlers and keeps request latency predictable.

## 2. High-Level Architecture

```text
                                      +----------------+
                                      |    Browser     |
                                      +-------+--------+
                                              |
                                              v
                                      +----------------+
                                      |     nginx      |
                                      +-------+--------+
                                              |
                        +---------------------+---------------------+
                        |                                           |
                        v                                           v
              +-------------------+                       +------------------+
              | Static frontend   |                       | /api/v1 reverse |
              | files             |                       | proxy            |
              +-------------------+                       +---------+--------+
                                                                  |
                                                                  v
                                                        +--------------------+
                                                        | C monitor backend  |
                                                        +--------------------+
```

Backend module layout:

```text
backend
├── main
├── config
├── http_server
├── api_handlers
├── sampler
├── snapshot
├── process_probe
├── gpu_probe_nvml
├── json_writer
├── auth_stub
├── ops_stub
└── platform
```

Frontend layout:

```text
frontend
├── src/api
├── src/pages/processes
├── src/pages/gpu
├── src/components/table
├── src/components/charts
├── src/components/layout
└── src/types
```

## 3. Technology Choices

### 3.1 Backend

Language:

- C11 or newer.

Core system libraries:

- `pthread` for the sampler thread and synchronization.
- Linux procfs for process and CPU metrics.
- NVML for NVIDIA GPU metrics.

Recommended third-party libraries:

- HTTP server: Mongoose or CivetWeb.
- JSON: yyjson.

Rationale:

- `pthread` is enough for a single sampler thread and a lightweight HTTP server integration.
- Using a mature embedded HTTP server avoids hand-rolling HTTP parsing and connection handling.
- yyjson provides fast JSON writing with a small C API surface.
- NVML is the standard supported NVIDIA interface for GPU utilization and GPU process data.

### 3.2 Frontend

Recommended stack:

- React.
- TypeScript.
- Vite.
- ECharts.
- TanStack Table or a local table wrapper.

Rationale:

- React and Vite provide fast development and simple static output.
- TypeScript keeps API contracts explicit.
- ECharts is sufficient for utilization charts and compact operational graphs.
- A table library reduces custom sorting, filtering, and rendering logic.

## 4. Backend Runtime Model

The backend starts in this order:

1. Parse command-line arguments.
2. Load and validate configuration.
3. Initialize logging.
4. Initialize snapshot store.
5. Initialize process probe.
6. Initialize NVML if GPU monitoring is enabled.
7. Start sampler thread.
8. Start HTTP server.
9. On shutdown, stop HTTP server, stop sampler, release NVML, and free snapshots.

Thread model:

```text
main thread
  └── HTTP server event loop

sampler thread
  └── periodic process and GPU collection
```

Optional future model:

- Add a dedicated operation worker for process control commands.
- Add a small thread pool only if API workload requires it.

## 5. Configuration Design

Configuration file format: TOML-like key-value file or strict INI-style key-value file. If a TOML parser is added, use a small existing C parser. For first implementation, a simple constrained key-value parser is acceptable if the accepted grammar is deliberately small.

Initial config:

```toml
listen_host = "127.0.0.1"
listen_port = 18080
sample_interval_ms = 1000
enable_gpu = true
enable_process_ops = false
enable_auth = false
```

Validation rules:

- `listen_host` must be a valid bind address string.
- `listen_port` must be in `1..65535`.
- `sample_interval_ms` should be at least `250`; recommended default is `1000`.
- `enable_process_ops` must default to `false`.
- `enable_auth` must default to `false`.

Configuration is read once at startup. Changes require restarting the service.

## 6. Snapshot Design

### 6.1 Snapshot Ownership

The sampler owns the construction of a new snapshot. Once fully built, it publishes the snapshot to the shared snapshot store.

HTTP handlers never mutate a published snapshot.

Recommended publication strategy:

- Build `next_snapshot` privately in the sampler thread.
- Acquire write lock.
- Swap `current_snapshot` with `next_snapshot`.
- Release write lock.
- Free the old snapshot outside the lock if practical.

HTTP handlers:

- Acquire read lock.
- Serialize required data from `current_snapshot`.
- Release read lock.

If serialization becomes too slow under many clients, a future optimization can pre-render common JSON responses during sampling.

### 6.2 Snapshot Data Model

Top-level snapshot:

```c
typedef struct monitor_snapshot {
    uint64_t sequence;
    int64_t sampled_at_ms;
    uint32_t sample_interval_ms;
    system_summary_t summary;
    process_list_t processes;
    gpu_list_t gpus;
    gpu_process_list_t gpu_processes;
    probe_status_t process_status;
    probe_status_t gpu_status;
} monitor_snapshot_t;
```

Process record:

```c
typedef struct process_info {
    int pid;
    int ppid;
    uint32_t uid;
    char user[64];
    char name[256];
    char state;
    char *cmdline;
    double cpu_percent;
    uint64_t rss_bytes;
    uint64_t vsize_bytes;
    int thread_count;
    int priority;
    int nice;
    uint64_t start_time_ticks;
    int64_t start_time_ms;
    uint64_t read_bytes;
    uint64_t write_bytes;
    bool has_io;
} process_info_t;
```

GPU record:

```c
typedef struct gpu_info {
    unsigned int index;
    char uuid[96];
    char name[128];
    unsigned int gpu_util_percent;
    unsigned int mem_util_percent;
    uint64_t mem_total_bytes;
    uint64_t mem_used_bytes;
    uint64_t mem_free_bytes;
    unsigned int temperature_c;
    unsigned int power_mw;
    unsigned int power_limit_mw;
    unsigned int fan_speed_percent;
    bool has_power;
    bool has_power_limit;
    bool has_fan_speed;
} gpu_info_t;
```

GPU process record:

```c
typedef struct gpu_process_info {
    unsigned int gpu_index;
    char gpu_uuid[96];
    int pid;
    uint64_t gpu_memory_bytes;
    char process_type[32];
    int process_index; /* Index into process list, or -1 when not found. */
} gpu_process_info_t;
```

## 7. Process Probe Design

### 7.1 Data Sources

Process list:

- Enumerate numeric directories under `/proc`.

Per-process files:

- `/proc/[pid]/stat`
- `/proc/[pid]/status`
- `/proc/[pid]/cmdline`
- `/proc/[pid]/io`

System files:

- `/proc/stat`
- `/proc/uptime`

### 7.2 CPU Usage Calculation

CPU usage for each process is calculated from deltas between two samples.

Inputs:

- Process user ticks and kernel ticks from `/proc/[pid]/stat`.
- Total CPU ticks from `/proc/stat`.
- Number of online CPUs from `sysconf(_SC_NPROCESSORS_ONLN)`.

Formula:

```text
process_delta = current_process_ticks - previous_process_ticks
system_delta = current_total_cpu_ticks - previous_total_cpu_ticks
cpu_percent = process_delta / system_delta * online_cpu_count * 100
```

Notes:

- The first sample cannot produce accurate per-process CPU percentages. Return `0` or mark as warming up.
- If a PID is reused, process start time from `/proc/[pid]/stat` must be used to distinguish old and new processes.
- If `system_delta == 0`, CPU percent should be `0`.

### 7.3 Process Parsing

`/proc/[pid]/stat` has a process name in parentheses and can contain spaces. The parser must locate the last `)` before parsing fields after it.

Important fields from `/proc/[pid]/stat`:

- `comm`
- `state`
- `ppid`
- `utime`
- `stime`
- `priority`
- `nice`
- `num_threads`
- `starttime`
- `vsize`
- `rss`

`/proc/[pid]/status` is used for:

- `Uid`
- optional fallback process name.

`/proc/[pid]/cmdline`:

- NUL-separated arguments must be converted to spaces for display.
- Empty cmdline can occur for kernel threads.

`/proc/[pid]/io`:

- `read_bytes`
- `write_bytes`
- This may fail due to permissions. Failure should set `has_io = false`.

### 7.4 UID To User Name Cache

Resolving user names through passwd lookups can become expensive. Use a small cache keyed by UID.

Requirements:

- Cache positive lookup results.
- Return UID string if no user name is found.
- Refreshing the cache during runtime is not required in version 1.

## 8. GPU Probe Design

### 8.1 NVML Initialization

If `enable_gpu = true`:

1. Call `nvmlInit_v2`.
2. Read driver version and NVML version.
3. Read device count.
4. Store initialization status.

If NVML initialization fails:

- Backend continues running.
- GPU endpoints return probe status with error details.
- Process endpoints remain available.

If `enable_gpu = false`:

- Skip NVML initialization.
- GPU endpoints return disabled status or an empty result with disabled metadata.

### 8.2 GPU Device Metrics

For each GPU:

- `nvmlDeviceGetHandleByIndex_v2`
- `nvmlDeviceGetUUID`
- `nvmlDeviceGetName`
- `nvmlDeviceGetUtilizationRates`
- `nvmlDeviceGetMemoryInfo`
- `nvmlDeviceGetTemperature`
- `nvmlDeviceGetPowerUsage`
- `nvmlDeviceGetEnforcedPowerLimit`
- `nvmlDeviceGetFanSpeed`

Each optional metric should have a `has_*` flag because some devices or drivers may not support every field.

### 8.3 GPU Process Metrics

Use NVML process query APIs, depending on driver support:

- Compute processes.
- Graphics processes when available.
- MPS processes when available, if needed later.

The GPU process list should be enriched by joining PID values with the latest process snapshot.

Join behavior:

- If a PID exists in process snapshot, include name, user, command line, CPU, RSS, and VSZ.
- If the PID is no longer present, keep the NVML PID and GPU memory usage, and mark process enrichment as unavailable.

## 9. HTTP API Design

### 9.1 Common Response Shape

Successful response:

```json
{
  "ok": true,
  "data": {},
  "meta": {
    "sequence": 12,
    "sampled_at_ms": 1760000000000,
    "sample_interval_ms": 1000
  }
}
```

Error response:

```json
{
  "ok": false,
  "error": {
    "code": "GPU_DISABLED",
    "message": "GPU monitoring is disabled by configuration"
  }
}
```

### 9.2 Endpoints

`GET /api/v1/config`

Returns non-sensitive runtime configuration:

```json
{
  "listen_host": "127.0.0.1",
  "listen_port": 18080,
  "sample_interval_ms": 1000,
  "enable_gpu": true,
  "enable_process_ops": false,
  "enable_auth": false
}
```

`GET /api/v1/system/summary`

Returns CPU, process, and GPU summary from the latest snapshot.

`GET /api/v1/processes`

Returns the process list from the latest snapshot.

Optional query parameters for future implementation:

```text
sort=cpu_percent
order=desc
q=python
limit=200
offset=0
```

Version 1 may return the full snapshot and let the frontend sort/filter locally.

`GET /api/v1/processes/{pid}`

Returns one process by PID from the latest snapshot. If the process is not found, return `404`.

`GET /api/v1/gpus`

Returns all GPU devices and probe status.

`GET /api/v1/gpus/{gpu_id}`

Returns one GPU by index or UUID. Numeric path segments should be treated as GPU index.

`GET /api/v1/gpu-processes`

Returns all GPU process records, enriched with process details when available.

Reserved process operation endpoints:

- Return `501 Not Implemented` in version 1.
- Later require auth and permission checks.
- Later write audit logs.

### 9.3 CORS And Reverse Proxy

Because nginx serves the frontend and proxies APIs under the same origin, CORS is not required for production.

During development, CORS can be enabled for the local Vite dev server if needed.

## 10. Frontend Design

### 10.1 Routes

```text
/processes
/gpu
```

The default route may redirect to `/processes`.

### 10.2 API Client

The frontend API client should:

- Load `/api/v1/config` during startup.
- Use `sample_interval_ms` as the default polling interval.
- Handle backend error response shape.
- Track stale data if `Date.now() - sampled_at_ms` exceeds a threshold, for example three polling intervals.

### 10.3 Process Page

Layout:

```text
top summary band
process toolbar
process table
process detail drawer
```

Summary metrics:

- Total CPU usage.
- Process count.
- Running process count.
- Last sample time.

Table columns:

- PID.
- User.
- Name.
- State.
- CPU%.
- RSS.
- VSZ.
- Threads.
- IO read.
- IO write.
- Command.

Interactions:

- Search input.
- Sortable columns.
- Row click opens detail drawer.
- Auto-refresh keeps selected PID if it still exists.

### 10.4 GPU Page

Layout:

```text
gpu overview band
gpu utilization charts
gpu device table/cards
gpu process table
process detail drawer
```

GPU overview:

- GPU count.
- Average GPU utilization.
- Total used memory.
- Highest temperature.

GPU process table:

- GPU index.
- PID.
- User.
- Name.
- GPU memory.
- CPU%.
- RSS.
- Command.

### 10.5 Visual Direction

The dashboard should be dense, operational, and easy to scan. It should avoid marketing-style hero sections. Cards should be used for repeated metric items or detail panels, not as decorative nested containers.

## 11. Future Authentication Design

Add an authentication module without changing handler business logic.

Planned types:

```c
typedef struct request_context {
    bool authenticated;
    char user_id[128];
    uint32_t permissions;
} request_context_t;
```

Each handler should accept or be able to derive a request context. Version 1 can return an anonymous context:

```text
authenticated = false
permissions = READ_METRICS
```

Future permission names:

- `READ_METRICS`
- `MANAGE_PROCESS`
- `MANAGE_CONFIG`
- `READ_AUDIT`

## 12. Future Process Operation Design

Reserved process operations:

- Kill process.
- Pause process with `SIGSTOP`.
- Resume process with `SIGCONT`.
- Adjust nice value.

Future safety requirements:

- Require authentication.
- Require `MANAGE_PROCESS` permission.
- Validate PID still matches expected start time to reduce PID reuse risk.
- Support dry-run mode if needed.
- Write audit records.
- Return structured operation result.

Potential future request:

```json
{
  "pid": 1234,
  "expected_start_time_ticks": 1234567,
  "signal": "SIGTERM"
}
```

## 13. Build And Deployment Design

### 13.1 Development

Backend:

```text
make
./monitor-backend --config ./config/monitor.conf
```

Frontend:

```text
npm install
npm run dev
```

Production frontend:

```text
npm run build
```

### 13.2 nginx Deployment

Example shape:

```nginx
server {
    listen 80;
    server_name monitor.local;

    root /opt/monitor-dashboard/frontend/dist;
    index index.html;

    location /api/v1/ {
        proxy_pass http://127.0.0.1:18080/api/v1/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }
}
```

### 13.3 systemd Deployment

Expected service behavior:

- Run as a dedicated user if permissions allow.
- Restart on failure.
- Read config from `/etc/monitor-dashboard/config.toml`.
- Log to journald.

NVML access usually works for normal users on typical NVIDIA driver installations, but deployment should verify permissions on the target host.

## 14. Error Handling

Error categories:

- `CONFIG_INVALID`
- `SNAPSHOT_NOT_READY`
- `PROCESS_NOT_FOUND`
- `GPU_DISABLED`
- `GPU_PROBE_UNAVAILABLE`
- `GPU_NOT_FOUND`
- `NOT_IMPLEMENTED`
- `INTERNAL_ERROR`

Sampler behavior:

- Process-level read errors do not fail the whole sample.
- GPU-level metric errors mark individual fields unavailable.
- Full NVML failure sets GPU probe status to failed but leaves process monitoring running.

Frontend behavior:

- Show API errors in the relevant page.
- Keep previously displayed data if a transient request fails, with stale marker.
- Show disabled GPU state if GPU monitoring is disabled.

## 15. Testing Strategy

Backend unit tests:

- Config parsing and validation.
- `/proc/[pid]/stat` parser, especially names containing spaces or parentheses.
- CPU usage delta calculation.
- JSON response shape.
- Snapshot swap behavior.

Backend integration tests:

- Start backend and call `/api/v1/config`.
- Call `/api/v1/processes` and verify current process visibility.
- Run with `enable_gpu = false` and verify GPU disabled response.
- Run on an NVIDIA host and verify `/api/v1/gpus`.

Frontend tests:

- API client response handling.
- Process table sorting and filtering.
- GPU disabled and GPU error states.
- Stale data indicator.

Manual verification:

- Compare process CPU with `top` or `ps`.
- Compare GPU metrics with `nvidia-smi`.
- Verify low backend overhead during 1-second polling.

## 16. Implementation Milestones

Milestone 1: Backend skeleton

- Config loader.
- HTTP server.
- `/api/v1/config`.
- Snapshot store.

Milestone 2: Process monitoring

- `/proc` scanner.
- CPU delta calculation.
- `/api/v1/system/summary`.
- `/api/v1/processes`.
- `/api/v1/processes/{pid}`.

Milestone 3: NVIDIA GPU monitoring

- NVML initialization.
- GPU device metrics.
- GPU process list.
- `/api/v1/gpus`.
- `/api/v1/gpu-processes`.

Milestone 4: Frontend

- React/Vite project.
- Process page.
- GPU page.
- Polling API client.
- Detail drawer.

Milestone 5: Deployment polish

- Production build.
- Example config.
- Example nginx config.
- Example systemd unit.
- README usage instructions.

