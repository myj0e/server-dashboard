import { useState, useEffect, useRef, useCallback } from 'react';
import type { SnapshotMeta } from '../types/api';

interface PollingState<T> {
  data: T | null;
  meta: SnapshotMeta | null;
  loading: boolean;
  error: string | null;
  isStale: boolean;
}

export function usePolling<T>(
  intervalMs: number,
  fetchFn: () => Promise<{ ok: boolean; data: T; meta?: SnapshotMeta; error?: { code: string; message: string } }>,
  enabled: boolean = true
): PollingState<T> {
  const [data, setData] = useState<T | null>(null);
  const [meta, setMeta] = useState<SnapshotMeta | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [isStale, setIsStale] = useState(false);
  const mountedRef = useRef(true);

  const tick = useCallback(async () => {
    try {
      const result = await fetchFn();
      if (!mountedRef.current) return;
      if (result.ok) {
        setData(result.data);
        setError(null);
        if (result.meta) {
          setMeta(result.meta);
          const staleThreshold = result.meta.sample_interval_ms * 3;
          const age = Date.now() - result.meta.sampled_at_ms;
          setIsStale(age > staleThreshold);
        }
      } else {
        setError(result.error?.message || 'Unknown error');
      }
    } catch (e) {
      if (mountedRef.current) {
        setError(e instanceof Error ? e.message : 'Network error');
      }
    } finally {
      if (mountedRef.current) {
        setLoading(false);
      }
    }
  }, [fetchFn]);

  useEffect(() => {
    mountedRef.current = true;
    if (!enabled) return;

    tick();
    const id = setInterval(tick, intervalMs);

    return () => {
      mountedRef.current = false;
      clearInterval(id);
    };
  }, [tick, intervalMs, enabled]);

  return { data, meta, loading, error, isStale };
}
