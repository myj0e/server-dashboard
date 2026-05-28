import { NavLink } from 'react-router-dom';
import styles from './Sidebar.module.css';

export default function Sidebar() {
  return (
    <aside className={styles.sidebar}>
      <div className={styles.brand}>
        <span className={styles.logo}>MD</span>
        <span className={styles.title}>监控面板</span>
      </div>
      <nav className={styles.nav}>
        <NavLink
          to="/processes"
          className={({ isActive }) => `${styles.link} ${isActive ? styles.active : ''}`}
        >
          <span className={styles.icon}>进</span>
          进程监控
        </NavLink>
        <NavLink
          to="/gpu"
          className={({ isActive }) => `${styles.link} ${isActive ? styles.active : ''}`}
        >
          <span className={styles.icon}>显</span>
          GPU 监控
        </NavLink>
      </nav>
    </aside>
  );
}
