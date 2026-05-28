import { useState } from 'react';
import type { Config } from '../../types/config';
import { useGpuData } from '../../hooks/useGpuData';
import { useProcessData } from '../../hooks/useProcessData';
import type { ProcessInfo } from '../../types/process';
import ErrorBanner from '../../components/shared/ErrorBanner';
import StaleIndicator from '../../components/shared/StaleIndicator';
import GpuOverviewBand from './GpuOverviewBand';
import GpuDeviceCard from './GpuDeviceCard';
import GpuProcessTable from './GpuProcessTable';
import ProcessDetail from '../processes/ProcessDetail';
import styles from './GpuPage.module.css';

interface Props {
  config: Config;
}

export default function GpuPage({ config }: Props) {
  const { data, meta, loading, error, isStale } = useGpuData(config.sample_interval_ms);
  const processData = useProcessData(config.sample_interval_ms);
  const [selectedPid, setSelectedPid] = useState<number | null>(null);

  const selectedProcess = selectedPid !== null
    ? processData.data?.processes.find((p: ProcessInfo) => p.pid === selectedPid) || null
    : null;

  if (!config.enable_gpu) {
    return (
      <div className={styles.page}>
        <ErrorBanner message="GPU 监控已在后端配置中禁用 (enable_gpu = false)" />
      </div>
    );
  }

  if (loading && !data) {
    return <div className={styles.status}>加载 GPU 数据中...</div>;
  }

  return (
    <div className={styles.page}>
      {error && <ErrorBanner message={error} />}

      <div className={styles.topRow}>
        <GpuOverviewBand gpus={data?.gpus || []} />
        <div className={styles.indicators}>
          {meta && (
            <StaleIndicator isStale={isStale} lastSampleMs={meta.sampled_at_ms} />
          )}
        </div>
      </div>

      <div className={styles.gpuCards}>
        {(data?.gpus || []).map((gpu) => (
          <GpuDeviceCard key={gpu.index} gpu={gpu} />
        ))}
      </div>

      <GpuProcessTable
        processes={data?.gpuProcesses || []}
        onSelect={(pid) => setSelectedPid(pid)}
      />

      <ProcessDetail
        process={selectedProcess}
        onClose={() => setSelectedPid(null)}
      />
    </div>
  );
}
