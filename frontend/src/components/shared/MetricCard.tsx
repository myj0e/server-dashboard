import styles from './MetricCard.module.css';

interface Props {
  label: string;
  value: string;
  sub?: string;
}

export default function MetricCard({ label, value, sub }: Props) {
  return (
    <div className={styles.card}>
      <span className={styles.label}>{label}</span>
      <span className={styles.value}>{value}</span>
      {sub && <span className={styles.sub}>{sub}</span>}
    </div>
  );
}
