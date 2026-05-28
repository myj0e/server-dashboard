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
        <MetricCard label="Processes" value="--" />
        <MetricCard label="Running" value="--" />
        <MetricCard label="Last Sample" value="--" />
      </div>
    );
  }

  return (
    <div className={styles.band}>
      <MetricCard label="Total CPU" value={formatPct(summary.cpu_percent)} />
      <MetricCard label="Processes" value={String(summary.process_count)} sub={`${summary.running_count} running`} />
      <MetricCard label="Sleeping" value={String(summary.sleeping_count)} />
      <MetricCard label="Zombie" value={String(summary.zombie_count)} />
      <MetricCard label="RAM Used" value={summary.used_ram_human} sub={summary.total_ram_human + ' total'} />
      <MetricCard label="CPUs" value={String(summary.online_cpus)} />
      <MetricCard label="Last Sample" value={formatMs(summary.sampled_at_ms)} />
    </div>
  );
}
