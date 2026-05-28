import type { GpuInfo } from '../../types/gpu';
import styles from './GpuDeviceCard.module.css';

interface Props {
  gpu: GpuInfo;
}

function Bar({ label, pct, color }: { label: string; pct: number; color: string }) {
  return (
    <div className={styles.barWrapper}>
      <span className={styles.barLabel}>{label}</span>
      <div className={styles.barTrack}>
        <div
          className={styles.barFill}
          style={{ width: `${Math.min(pct, 100)}%`, background: color }}
        />
      </div>
      <span className={styles.barValue}>{pct}%</span>
    </div>
  );
}

export default function GpuDeviceCard({ gpu }: Props) {
  const memUsedPct = gpu.mem_total_bytes > 0
    ? Math.round((gpu.mem_used_bytes / gpu.mem_total_bytes) * 100)
    : 0;

  return (
    <div className={styles.card}>
      <div className={styles.header}>
        <span className={styles.name}>{gpu.name}</span>
        <span className={styles.index}>GPU {gpu.index}</span>
      </div>

      <div className={styles.bars}>
        <Bar label="GPU" pct={gpu.gpu_util_percent} color="var(--accent)" />
        <Bar label="Mem" pct={memUsedPct} color="var(--success)" />
      </div>

      <div className={styles.details}>
        <div className={styles.detailRow}>
          <span className={styles.detailLabel}>Memory</span>
          <span className={styles.detailValue}>
            {gpu.mem_used_human} / {gpu.mem_total_human}
          </span>
        </div>
        <div className={styles.detailRow}>
          <span className={styles.detailLabel}>Temperature</span>
          <span className={styles.detailValue}>{gpu.temperature_c}°C</span>
        </div>
        {gpu.has_power && (
          <div className={styles.detailRow}>
            <span className={styles.detailLabel}>Power</span>
            <span className={styles.detailValue}>
              {gpu.power_mw / 1000}W
              {gpu.has_power_limit ? ` / ${gpu.power_limit_mw / 1000}W` : ''}
            </span>
          </div>
        )}
        {gpu.has_fan_speed && (
          <div className={styles.detailRow}>
            <span className={styles.detailLabel}>Fan</span>
            <span className={styles.detailValue}>{gpu.fan_speed_percent}%</span>
          </div>
        )}
        <div className={styles.detailRow}>
          <span className={styles.detailLabel}>Driver</span>
          <span className={styles.detailValue}>{gpu.driver_version}</span>
        </div>
        <div className={styles.detailRow}>
          <span className={styles.detailLabel}>UUID</span>
          <span className={styles.detailValueMono}>{gpu.uuid}</span>
        </div>
      </div>
    </div>
  );
}
