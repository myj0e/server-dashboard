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
      Data may be stale ({ageSec}s since last sample)
    </div>
  );
}
