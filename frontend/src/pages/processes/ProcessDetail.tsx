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
  if (days > 0) parts.push(`${days}天`);
  if (hours > 0) parts.push(`${hours}时`);
  if (minutes > 0) parts.push(`${minutes}分`);
  parts.push(`${seconds}秒`);
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
          <Row label="父进程" value={String(process.ppid)} />
          <Row label="状态" value={process.state} />
          <Row label="用户" value={`${process.user} (UID ${process.uid})`} />
          <Row label="进程名" value={process.name} />
          <Row label="CPU 占用" value={process.cpu_percent.toFixed(2) + '%'} />
          <Row label="物理内存" value={process.rss_human} />
          <Row label="虚拟内存" value={process.vsize_human} />
          <Row label="线程数" value={String(process.thread_count)} />
          <Row label="优先级" value={String(process.priority)} />
          <Row label="Nice值" value={String(process.nice)} />
          <Row label="启动时间" value={formatTime(process.start_time_ms)} />
          <Row label="运行时长" value={formatMs(process.running_duration_ms)} />
          <Row
            label="读取字节"
            value={process.has_io && process.read_bytes != null
              ? String(process.read_bytes) : '不可用'}
          />
          <Row
            label="写入字节"
            value={process.has_io && process.write_bytes != null
              ? String(process.write_bytes) : '不可用'}
          />
          <div className={styles.fullRow}>
            <span className={styles.label}>命令行</span>
            <pre className={styles.cmdline}>{process.cmdline || '(无)'}</pre>
          </div>
        </div>
      )}
    </Drawer>
  );
}
