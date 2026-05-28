export interface ApiResponse<T> {
  ok: boolean;
  data: T;
  meta?: SnapshotMeta;
  error?: ApiError;
}

export interface SnapshotMeta {
  sequence: number;
  sampled_at_ms: number;
  sample_interval_ms: number;
}

export interface ApiError {
  code: string;
  message: string;
}

export interface ProbeStatus {
  status: 'ok' | 'disabled' | 'error' | 'warming_up';
  error_message: string;
  last_ok_sample_ms: number;
}
