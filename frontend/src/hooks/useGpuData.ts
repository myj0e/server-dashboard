import { useCallback } from 'react';
import { usePolling } from './usePolling';
import { fetchGpus, fetchGpuProcesses } from '../api/client';
import type { GpuInfo, GpuProcessInfo } from '../types/gpu';

export interface GpuData {
  gpus: GpuInfo[];
  gpuProcesses: GpuProcessInfo[];
}

export function useGpuData(intervalMs: number) {
  const fetchFn = useCallback(async () => {
    const [gpuRes, gpRes] = await Promise.all([
      fetchGpus(),
      fetchGpuProcesses(),
    ]);

    // If GPU is disabled, return empty
    if (!gpuRes.ok) {
      return {
        ok: true, // Not a fatal error, just no GPU data
        data: {
          gpus: [],
          gpuProcesses: [],
        } as GpuData,
        meta: gpuRes.meta,
      };
    }

    return {
      ok: true,
      data: {
        gpus: gpuRes.data?.gpus || [],
        gpuProcesses: gpRes.ok ? (gpRes.data?.gpu_processes || []) : [],
      } as GpuData,
      meta: gpuRes.meta,
    };
  }, []);

  return usePolling<GpuData>(intervalMs, fetchFn);
}
