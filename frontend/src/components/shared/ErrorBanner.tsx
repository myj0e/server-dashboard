import styles from './ErrorBanner.module.css';

interface Props {
  message: string;
}

export default function ErrorBanner({ message }: Props) {
  return (
    <div className={styles.banner}>
      <span className={styles.icon}>!</span>
      {message}
    </div>
  );
}
