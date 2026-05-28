import type { ProbeStatus } from './api';

export interface GpuInfo {
  index: number;
  uuid: string;
  name: string;
  gpu_util_percent: number;
  mem_util_percent: number;
  mem_total_bytes: number;
  mem_used_bytes: number;
  mem_free_bytes: number;
  temperature_c: number;
  power_mw: number;
  power_limit_mw: number;
  fan_speed_percent: number;
  has_power: boolean;
  has_power_limit: boolean;
  has_fan_speed: boolean;
  driver_version: string;
  nvml_version: string;
  mem_total_human: string;
  mem_used_human: string;
}

export interface GpuProcessInfo {
  gpu_index: number;
  gpu_uuid: string;
  pid: number;
  gpu_memory_bytes: number;
  process_type: string;
  enriched: boolean;
  user?: string;
  name?: string;
  cmdline?: string;
  cpu_percent?: number;
  rss_bytes?: number;
  vsize_bytes?: number;
}

export interface GpuListResponse {
  gpus: GpuInfo[];
  gpu_probe: ProbeStatus;
}

export interface GpuProcessListResponse {
  gpu_processes: GpuProcessInfo[];
  gpu_probe: ProbeStatus;
}

export interface GpuDetailResponse {
  gpu: GpuInfo;
}
