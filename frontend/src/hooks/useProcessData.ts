import { useCallback } from 'react';
import { usePolling } from './usePolling';
import { fetchProcesses, fetchSystemSummary } from '../api/client';
import type { ProcessInfo } from '../types/process';
import type { SystemSummary } from '../types/system';

export interface ProcessData {
  processes: ProcessInfo[];
  summary: SystemSummary | null;
}

export function useProcessData(intervalMs: number) {
  const fetchFn = useCallback(async () => {
    const [procRes, summaryRes] = await Promise.all([
      fetchProcesses(),
      fetchSystemSummary(),
    ]);

    if (!procRes.ok) {
      return { ok: false, data: {} as ProcessData, error: procRes.error };
    }

    return {
      ok: true,
      data: {
        processes: procRes.data?.processes || [],
        summary: summaryRes.ok ? summaryRes.data.summary : null,
      } as ProcessData,
      meta: procRes.meta,
    };
  }, []);

  return usePolling<ProcessData>(intervalMs, fetchFn);
}
