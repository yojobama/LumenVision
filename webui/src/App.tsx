import React, { useState, useEffect } from 'react';
import { Routes, Route, NavLink, useNavigate } from 'react-router-dom';
import {
  Settings,
  Sun,
  Moon,
  RefreshCw,
  Target,
  BarChart3,
  Wifi,
  WifiOff,
  AlertTriangle,
  X,
  Workflow,
  Gauge,
  Layers,
  Film,
} from 'lucide-react';
import './App.css';
import StereoPage from './components/StereoPage';
import { DashboardPage } from './pages/DashboardPage';
import { SettingsPage } from './pages/SettingsPage';
import { GraphPage } from './pages/GraphPage';
import { MatchPage } from './pages/MatchPage';
import { RecordingsPage } from './pages/RecordingsPage';
import { CalibrationWizardPage } from './pages/CalibrationWizardPage';
import { StereoCalibrationWizardPage } from './pages/StereoCalibrationWizardPage';

import type { SystemStats, Settings as SettingsType } from './types';

import { Toast } from './components/Toast';
import { useAppData } from './hooks/useAppData';

const useDarkMode = () => {
  const [darkMode, setDarkMode] = useState(() => {
    const saved = localStorage.getItem('darkMode');
    return saved ? JSON.parse(saved) : true;
  });

  useEffect(() => {
    localStorage.setItem('darkMode', JSON.stringify(darkMode));
    if (darkMode) {
      document.documentElement.classList.add('dark');
    } else {
      document.documentElement.classList.remove('dark');
    }
  }, [darkMode]);

  return [darkMode, setDarkMode] as const;
}

const Header: React.FC<{
  systemStats: SystemStats;
  darkMode: boolean;
  onToggleDarkMode: () => void;
  onRefresh: () => void;
}> = ({ systemStats, darkMode, onToggleDarkMode, onRefresh }) => {
  const navigate = useNavigate();
  return (
  <header className="bg-white dark:bg-gray-800 shadow-sm border-b border-gray-200 dark:border-gray-700">
    <div className="px-6 py-4">
      <div className="flex items-center justify-between">
        <div className="flex items-center space-x-4">
          <div className="flex items-center space-x-2">
            <Target className="w-8 h-8 text-blue-600" />
            <h1 className="text-2xl font-bold text-gray-900 dark:text-white">LumenVision</h1>
          </div>
          <span className={`px-3 py-1 rounded-full text-sm font-medium flex items-center gap-1 ${
            systemStats.serverStatus === 'online'
              ? 'bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-200'
              : systemStats.serverStatus === 'error'
              ? 'bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200'
              : 'bg-gray-100 text-gray-800 dark:bg-gray-700 dark:text-gray-300'
          }`}>
            {systemStats.serverStatus === 'online' ?
              <>
                <Wifi className="w-4 h-4" />Connected
              </> :
             systemStats.serverStatus === 'error' ?
              <>
                <AlertTriangle className="w-4 h-4" />Error
              </> :
              <>
                <WifiOff className="w-4 h-4" />Disconnected
              </>}
          </span>
        </div>
        <div className="flex items-center space-x-4">
          <button onClick={() => navigate('/settings')} className="p-2 rounded-lg bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 hover:bg-gray-200 dark:hover:bg-gray-600" title="Settings">
            <Settings className="w-5 h-5" />
          </button>
          <button onClick={onToggleDarkMode} className="p-2 rounded-lg bg-gray-100 dark:bg-gray-700 text-gray-600 dark:text-gray-300 hover:bg-gray-200 dark:hover:bg-gray-600" title="Toggle dark mode">
            {darkMode ? <Sun className="w-5 h-5" /> : <Moon className="w-5 h-5" />}
          </button>
          <button onClick={onRefresh} className="px-3 py-2 bg-blue-600 text-white rounded hover:bg-blue-700 flex items-center gap-2" title="Refresh data">
            <RefreshCw className="w-4 h-4" />Refresh
          </button>
        </div>
      </div>
    </div>
  </header>
  );
};

const NAV_ITEMS = [
  { to: '/', label: 'Dashboard', icon: BarChart3, end: true },
  { to: '/graph', label: 'Graph', icon: Workflow, end: false },
  { to: '/stereo', label: 'Stereo', icon: Layers, end: false },
  { to: '/match', label: 'Match', icon: Gauge, end: false },
  { to: '/recordings', label: 'Recordings', icon: Film, end: false },
];

const Navigation: React.FC<{ streamingCount: number }> = ({ streamingCount }) => (
  <nav className="bg-white dark:bg-gray-800 border-b border-gray-200 dark:border-gray-700">
    <div className="px-6">
      <div className="flex space-x-8">
        {NAV_ITEMS.map(item => (
          <NavLink
            key={item.to}
            to={item.to}
            end={item.end}
            className={({ isActive }) => `flex items-center space-x-2 py-4 px-2 border-b-2 font-medium text-sm transition-colors ${isActive ? 'border-blue-500 text-blue-600 dark:text-blue-400' : 'border-transparent text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-300'}`}
          >
            <item.icon className="w-4 h-4" />
            <span>{item.label}</span>
            {item.to === '/' && streamingCount > 0 && (
              <span className="bg-red-500 text-white text-xs rounded-full px-2 py-0.5 min-w-[20px] h-5 flex items-center justify-center">{streamingCount}</span>
            )}
          </NavLink>
        ))}
      </div>
    </div>
  </nav>
);

function App() {
  const navigate = useNavigate();
  const [darkMode, setDarkMode] = useDarkMode();
  const [settings, setSettingsState] = useState<SettingsType>(() => {
    try {
      const saved = localStorage.getItem('lumenSettings');
      if (saved) return { ...JSON.parse(saved), serverUrl: window.location.origin };
    } catch { /* ignore malformed/unavailable localStorage, fall through to defaults */ }
    return { serverUrl: window.location.origin, nt4: { mode: 'team', rootTable: 'lumenvision' } };
  });
  // Persist NT4 connection details across reloads.
  const setSettings = (next: SettingsType) => {
    setSettingsState(next);
    try { localStorage.setItem('lumenSettings', JSON.stringify(next)); } catch { /* ignore */ }
  };
  const {
    sources,
    sinks,
    streamingSinks,
    loading,
    error,
    systemStats,
    deviceStats,
    toast,
    loadData,
    stopStream,
    handleStreamError,
    showToast,
    handleToggleSink,
    handleTogglePreview,
    handleToggleNT4Publish,
    setStreamingSinks,
    setError,
    setToast
  } = useAppData();

  const toggleDarkMode = () => {
    setDarkMode(!darkMode);
    showToast('Theme updated successfully', 'success');
  };

  if (loading) {
    return (
      <div className="min-h-screen bg-gray-50 dark:bg-gray-900 flex items-center justify-center">
        <div className="text-center">
          <div className="animate-spin rounded-full h-32 w-32 border-b-2 border-blue-600 mx-auto mb-4">
            <RefreshCw className="w-32 h-32 text-blue-600" />
          </div>
          <p className="text-gray-600 dark:text-gray-400 text-lg">Loading LumenVision...</p>
          <p className="text-gray-500 dark:text-gray-500 text-sm mt-2">Connecting to {settings.serverUrl}</p>
        </div>
      </div>
    );
  }

  return (
    <div className={`min-h-screen transition-colors duration-200 ${darkMode ? 'dark' : ''}`}>
      <div className="min-h-screen bg-gray-50 dark:bg-gray-900">
        <Header systemStats={systemStats} darkMode={darkMode} onToggleDarkMode={toggleDarkMode} onRefresh={loadData} />
        <Navigation streamingCount={streamingSinks.size} />

        {error && (
          <div className="bg-red-100 dark:bg-red-900 border-l-4 border-red-500 p-4 mx-6 mt-4 rounded">
            <div className="flex">
              <div className="flex-shrink-0"><AlertTriangle className="w-5 h-5 text-red-500" /></div>
              <div className="ml-3"><p className="text-sm text-red-700 dark:text-red-200">Connection Error: {error}</p></div>
              <div className="ml-auto pl-3">
                <button onClick={()=>setError(null)} className="text-red-500 hover:text-red-700"><X className="w-4 h-4" /></button>
              </div>
            </div>
          </div>
        )}

        <main className="p-6">
          <Routes>
            <Route path="/" element={
              <DashboardPage
                systemStats={systemStats}
                deviceStats={deviceStats}
                streamingSinks={streamingSinks}
                sources={sources}
                sinks={sinks}
                onStopAllStreams={()=>setStreamingSinks(new Set())}
                onStopStream={stopStream}
                onStreamError={handleStreamError}
                onTogglePreview={handleTogglePreview}
                onGoToSinks={()=>navigate('/graph')}
                onGoToSources={()=>navigate('/graph')}
                onToggleSink={handleToggleSink}
              />
            } />
            <Route path="/graph" element={<GraphPage onToast={showToast} nt4Settings={settings.nt4} darkMode={darkMode} />} />
            <Route path="/stereo" element={
              <StereoPage sources={sources} sinks={sinks} onToast={showToast} onRefresh={loadData} />
            } />
            <Route path="/match" element={<MatchPage />} />
            <Route path="/recordings" element={<RecordingsPage onToast={showToast} />} />
            <Route path="/settings" element={<SettingsPage settings={settings} onSave={setSettings} />} />
            <Route path="/calibrate/stereo/:sinkId" element={<StereoCalibrationWizardPage sources={sources} onToast={showToast} />} />
            <Route path="/calibrate/:sinkId" element={<CalibrationWizardPage sources={sources} onToast={showToast} />} />
          </Routes>
        </main>

        {toast && <Toast message={toast.message} type={toast.type} onClose={()=>setToast(null)} />}
      </div>
    </div>
  );
}

export default App;
