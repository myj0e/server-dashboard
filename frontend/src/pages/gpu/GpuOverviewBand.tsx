import type { GpuInfo } from '../../types/gpu';
import MetricCard from '../../components/shared/MetricCard';
import styles from './GpuOverviewBand.module.css';

interface Props {
  gpus: GpuInfo[];
}

function formatBytes(bytes: number): string {
  if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + ' GB';
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(0) + ' MB';
  return (bytes / 1024).toFixed(0) + ' KB';
}

export default function GpuOverviewBand({ gpus }: Props) {
  if (gpus.length === 0) {
    return (
      <div className={styles.band}>
        <MetricCard label="GPUs" value="0" />
      </div>
    );
  }

  const avgUtil = gpus.reduce((s, g) => s + g.gpu_util_percent, 0) / gpus.length;
  const totalMem = gpus.reduce((s, g) => s + g.mem_used_bytes, 0);
  const maxTemp = gpus.reduce((m, g) => g.temperature_c > m ? g.temperature_c : m, 0);

  return (
    <div className={styles.band}>
      <MetricCard label="GPUs" value={String(gpus.length)} />
      <MetricCard label="Avg Util" value={avgUtil.toFixed(0) + '%'} />
      <MetricCard label="GPU Mem Used" value={formatBytes(totalMem)} />
      <MetricCard label="Max Temp" value={maxTemp + '°C'} />
    </div>
  );
}
