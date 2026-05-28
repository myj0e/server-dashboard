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
          setConfigError(res.error?.message || '加载配置失败');
        }
      })
      .catch((e) => setConfigError(e.message));
  }, []);

  if (configError) {
    return (
      <div className={styles.errorFullscreen}>
        <h2>连接错误</h2>
        <p>{configError}</p>
        <p className={styles.hint}>请确认后端已在 18080 端口运行</p>
      </div>
    );
  }

  if (!config) {
    return (
      <div className={styles.loadingFullscreen}>
        <p>正在连接后端服务...</p>
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
