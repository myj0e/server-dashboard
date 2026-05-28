import type { ProcessInfo } from '../../types/process';
import Drawer from '../../components/shared/Drawer';
import styles from './ProcessDetail.module.css';

interface Props {
  process: ProcessInfo | null;
  onClose: () => void;
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className={styles.row}>
      <span className={styles.label}>{label}</span>
      <span className={styles.value}>{value}</span>
    </div>
  );
}

function formatMs(ms: number): string {
  const days = Math.floor(ms / 86400000);
  const hours = Math.floor((ms % 86400000) / 3600000);
  const minutes = Math.floor((ms % 3600000) / 60000);
  const seconds = Math.floor((ms % 60000) / 1000);
  const parts: string[] = [];
  if (days > 0) parts.push(`${days}d`);
  if (hours > 0) parts.push(`${hours}h`);
  if (minutes > 0) parts.push(`${minutes}m`);
  parts.push(`${seconds}s`);
  return parts.join(' ');
}

function formatTime(ms: number): string {
  return new Date(ms).toISOString().replace('T', ' ').slice(0, 19);
}

export default function ProcessDetail({ process, onClose }: Props) {
  return (
    <Drawer
      open={process !== null}
      onClose={onClose}
      title={process ? `PID ${process.pid} - ${process.name}` : ''}
    >
      {process && (
        <div className={styles.grid}>
          <Row label="PID" value={String(process.pid)} />
          <Row label="PPID" value={String(process.ppid)} />
          <Row label="State" value={process.state} />
          <Row label="User" value={`${process.user} (UID ${process.uid})`} />
          <Row label="Name" value={process.name} />
          <Row label="CPU %" value={process.cpu_percent.toFixed(2)} />
          <Row label="RSS" value={process.rss_human} />
          <Row label="VSZ" value={process.vsize_human} />
          <Row label="Threads" value={String(process.thread_count)} />
          <Row label="Priority" value={String(process.priority)} />
          <Row label="Nice" value={String(process.nice)} />
          <Row label="Start Time" value={formatTime(process.start_time_ms)} />
          <Row label="Running For" value={formatMs(process.running_duration_ms)} />
          <Row
            label="IO Read"
            value={process.has_io && process.read_bytes != null
              ? String(process.read_bytes) : 'unavailable'}
          />
          <Row
            label="IO Write"
            value={process.has_io && process.write_bytes != null
              ? String(process.write_bytes) : 'unavailable'}
          />
          <div className={styles.fullRow}>
            <span className={styles.label}>Command Line</span>
            <pre className={styles.cmdline}>{process.cmdline || '(none)'}</pre>
          </div>
        </div>
      )}
    </Drawer>
  );
}
