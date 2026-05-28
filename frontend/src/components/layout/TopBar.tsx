import type { Config } from '../../types/config';
import styles from './TopBar.module.css';

interface Props {
  config: Config;
}

export default function TopBar({ config }: Props) {
  return (
    <header className={styles.topbar}>
      <div className={styles.info}>
        <span className={styles.label}>Interval</span>
        <span className={styles.value}>{config.sample_interval_ms}ms</span>
      </div>
      <div className={styles.info}>
        <span className={styles.label}>GPU</span>
        <span className={`${styles.value} ${config.enable_gpu ? styles.on : styles.off}`}>
          {config.enable_gpu ? 'ON' : 'OFF'}
        </span>
      </div>
      <div className={styles.spacer} />
      <div className={styles.info}>
        <span className={styles.label}>Backend</span>
        <span className={styles.value}>{config.listen_host}:{config.listen_port}</span>
      </div>
    </header>
  );
}
