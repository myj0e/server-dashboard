import type { Config } from '../../types/config';
import styles from './TopBar.module.css';

interface Props {
  config: Config;
}

export default function TopBar({ config }: Props) {
  return (
    <header className={styles.topbar}>
      <div className={styles.info}>
        <span className={styles.label}>采样间隔</span>
        <span className={styles.value}>{config.sample_interval_ms}ms</span>
      </div>
      <div className={styles.info}>
        <span className={styles.label}>GPU</span>
        <span className={`${styles.value} ${config.enable_gpu ? styles.on : styles.off}`}>
          {config.enable_gpu ? '开' : '关'}
        </span>
      </div>
      <div className={styles.spacer} />
      <div className={styles.info}>
        <span className={styles.label}>后端地址</span>
        <span className={styles.value}>{config.listen_host}:{config.listen_port}</span>
      </div>
    </header>
  );
}
