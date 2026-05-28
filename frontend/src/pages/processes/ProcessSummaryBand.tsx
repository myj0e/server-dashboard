import type { SystemSummary } from '../../types/system';
import MetricCard from '../../components/shared/MetricCard';
import styles from './ProcessSummaryBand.module.css';

interface Props {
  summary: SystemSummary | null;
}

function formatPct(v: number): string {
  return v.toFixed(1) + '%';
}

function formatMs(ms: number): string {
  const d = new Date(ms);
  return d.toLocaleTimeString();
}

export default function ProcessSummaryBand({ summary }: Props) {
  if (!summary) {
    return (
      <div className={styles.band}>
        <MetricCard label="CPU" value="--" />
        <MetricCard label="进程数" value="--" />
        <MetricCard label="运行中" value="--" />
        <MetricCard label="最新采样" value="--" />
      </div>
    );
  }

  return (
    <div className={styles.band}>
      <MetricCard label="总CPU" value={formatPct(summary.cpu_percent)} />
      <MetricCard label="进程数" value={String(summary.process_count)} sub={`${summary.running_count} 运行中`} />
      <MetricCard label="睡眠" value={String(summary.sleeping_count)} />
      <MetricCard label="僵尸" value={String(summary.zombie_count)} />
      <MetricCard label="已用内存" value={summary.used_ram_human} sub={summary.total_ram_human + ' 总计'} />
      <MetricCard label="CPU核心" value={String(summary.online_cpus)} />
      <MetricCard label="最新采样" value={formatMs(summary.sampled_at_ms)} />
    </div>
  );
}
