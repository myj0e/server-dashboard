import { NavLink } from 'react-router-dom';
import styles from './Sidebar.module.css';

export default function Sidebar() {
  return (
    <aside className={styles.sidebar}>
      <div className={styles.brand}>
        <span className={styles.logo}>MD</span>
        <span className={styles.title}>Monitor</span>
      </div>
      <nav className={styles.nav}>
        <NavLink
          to="/processes"
          className={({ isActive }) => `${styles.link} ${isActive ? styles.active : ''}`}
        >
          <span className={styles.icon}>CPU</span>
          Processes
        </NavLink>
        <NavLink
          to="/gpu"
          className={({ isActive }) => `${styles.link} ${isActive ? styles.active : ''}`}
        >
          <span className={styles.icon}>GPU</span>
          GPU
        </NavLink>
      </nav>
    </aside>
  );
}
