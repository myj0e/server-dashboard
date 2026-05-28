import styles from './StaleIndicator.module.css';

interface Props {
  isStale: boolean;
  lastSampleMs: number;
}

export default function StaleIndicator({ isStale, lastSampleMs }: Props) {
  const age = Date.now() - lastSampleMs;
  const ageSec = Math.round(age / 1000);

  if (!isStale) return null;

  return (
    <div className={styles.indicator}>
      数据可能已过期 (距上次采样 {ageSec} 秒)
    </div>
  );
}
