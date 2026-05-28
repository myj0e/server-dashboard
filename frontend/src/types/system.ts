import type { ProbeStatus } from './api';

export interface SystemSummary {
  sampled_at_ms: number;
  online_cpus: number;
  cpu_percent: number;
  total_ram_bytes: number;
  used_ram_bytes: number;
  total_swap_bytes: number;
  used_swap_bytes: number;
  process_count: number;
  running_count: number;
  sleeping_count: number;
  zombie_count: number;
  total_ram_human: string;
  used_ram_human: string;
}

export interface SystemSummaryResponse {
  summary: SystemSummary;
  process_probe: ProbeStatus;
  gpu_probe: ProbeStatus;
}
