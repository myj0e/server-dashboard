import type { ProbeStatus } from './api';

export interface ProcessInfo {
  pid: number;
  ppid: number;
  uid: number;
  user: string;
  name: string;
  state: string;
  cmdline: string | null;
  cpu_percent: number;
  rss_bytes: number;
  vsize_bytes: number;
  thread_count: number;
  priority: number;
  nice: number;
  start_time_ms: number;
  running_duration_ms: number;
  read_bytes: number | null;
  write_bytes: number | null;
  has_io: boolean;
  rss_human: string;
  vsize_human: string;
}

export interface ProcessListResponse {
  processes: ProcessInfo[];
  process_probe: ProbeStatus;
}

export interface ProcessDetailResponse {
  process: ProcessInfo;
}
