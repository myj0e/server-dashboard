export interface Config {
  listen_host: string;
  listen_port: number;
  sample_interval_ms: number;
  enable_gpu: boolean;
  enable_process_ops: boolean;
  enable_auth: boolean;
}
