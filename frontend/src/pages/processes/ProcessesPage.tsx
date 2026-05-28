import { useState } from 'react';
import type { Config } from '../../types/config';
import type { ProcessInfo } from '../../types/process';
import { useProcessData } from '../../hooks/useProcessData';
import ErrorBanner from '../../components/shared/ErrorBanner';
import StaleIndicator from '../../components/shared/StaleIndicator';
import ProcessSummaryBand from './ProcessSummaryBand';
import ProcessTable from './ProcessTable';
import ProcessDetail from './ProcessDetail';
import styles from './ProcessesPage.module.css';

interface Props {
  config: Config;
}

export default function ProcessesPage({ config }: Props) {
  const { data, meta, loading, error, isStale } = useProcessData(config.sample_interval_ms);
  const [selectedPid, setSelectedPid] = useState<number | null>(null);

  const selectedProcess = selectedPid !== null
    ? data?.processes.find((p) => p.pid === selectedPid) || null
    : null;

  if (loading && !data) {
    return <div className={styles.status}>加载进程数据中...</div>;
  }

  return (
    <div className={styles.page}>
      {error && <ErrorBanner message={error} />}

      <div className={styles.topRow}>
        <ProcessSummaryBand summary={data?.summary || null} />
        <div className={styles.indicators}>
          {meta && (
            <StaleIndicator isStale={isStale} lastSampleMs={meta.sampled_at_ms} />
          )}
        </div>
      </div>

      <ProcessTable
        processes={data?.processes || []}
        onSelect={(pid) => setSelectedPid(pid)}
      />

      <ProcessDetail
        process={selectedProcess}
        onClose={() => setSelectedPid(null)}
      />
    </div>
  );
}
