import type { ApiResponse } from '../types/api';
import type { Config } from '../types/config';
import type { SystemSummaryResponse } from '../types/system';
import type { ProcessListResponse, ProcessDetailResponse } from '../types/process';
import type { GpuListResponse, GpuDetailResponse, GpuProcessListResponse } from '../types/gpu';

const BASE = '/api/v1';

async function get<T>(path: string): Promise<ApiResponse<T>> {
  const res = await fetch(`${BASE}${path}`);
  if (!res.ok) {
    const body = await res.json().catch(() => ({}));
    return {
      ok: false,
      data: {} as T,
      error: {
        code: `HTTP_${res.status}`,
        message: body?.error?.message || res.statusText,
      },
    };
  }
  return res.json();
}

export function fetchConfig(): Promise<ApiResponse<Config>> {
  return get<Config>('/config');
}

export function fetchSystemSummary(): Promise<ApiResponse<SystemSummaryResponse>> {
  return get<SystemSummaryResponse>('/system/summary');
}

export function fetchProcesses(): Promise<ApiResponse<ProcessListResponse>> {
  return get<ProcessListResponse>('/processes');
}

export function fetchProcessByPid(pid: number): Promise<ApiResponse<ProcessDetailResponse>> {
  return get<ProcessDetailResponse>(`/processes/${pid}`);
}

export function fetchGpus(): Promise<ApiResponse<GpuListResponse>> {
  return get<GpuListResponse>('/gpus');
}

export function fetchGpuById(id: number): Promise<ApiResponse<GpuDetailResponse>> {
  return get<GpuDetailResponse>(`/gpus/${id}`);
}

export function fetchGpuProcesses(): Promise<ApiResponse<GpuProcessListResponse>> {
  return get<GpuProcessListResponse>('/gpu-processes');
}
