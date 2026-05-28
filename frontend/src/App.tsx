import { Routes, Route, Navigate } from 'react-router-dom';
import { useState, useEffect } from 'react';
import { fetchConfig } from './api/client';
import type { Config } from './types/config';
import Sidebar from './components/layout/Sidebar';
import TopBar from './components/layout/TopBar';
import ProcessesPage from './pages/processes/ProcessesPage';
import GpuPage from './pages/gpu/GpuPage';
import styles from './App.module.css';

export default function App() {
  const [config, setConfig] = useState<Config | null>(null);
  const [configError, setConfigError] = useState<string | null>(null);

  useEffect(() => {
    fetchConfig()
      .then((res) => {
        if (res.ok) {
          setConfig(res.data);
        } else {
          setConfigError(res.error?.message || 'Failed to load config');
        }
      })
      .catch((e) => setConfigError(e.message));
  }, []);

  if (configError) {
    return (
      <div className={styles.errorFullscreen}>
        <h2>Connection Error</h2>
        <p>{configError}</p>
        <p className={styles.hint}>Make sure the backend is running on port 18080</p>
      </div>
    );
  }

  if (!config) {
    return (
      <div className={styles.loadingFullscreen}>
        <p>Connecting to monitor backend...</p>
      </div>
    );
  }

  return (
    <div className={styles.app}>
      <Sidebar />
      <div className={styles.main}>
        <TopBar config={config} />
        <div className={styles.content}>
          <Routes>
            <Route path="/processes" element={<ProcessesPage config={config} />} />
            <Route path="/gpu" element={<GpuPage config={config} />} />
            <Route path="*" element={<Navigate to="/processes" replace />} />
          </Routes>
        </div>
      </div>
    </div>
  );
}
