import React from 'react';
import { NavLink } from 'react-router-dom';
import {
  ChevronDown, ChevronRight, Layers, Camera, Box, Grid3x3, ScrollText,
  Settings as SettingsIcon, PlayCircle, RefreshCw, Film,
} from 'lucide-react';
import { ApiService } from '../services/ApiService';
import type { StateSnapshot, Model, StoredCameraCalibration } from '../types';

const api = new ApiService();

type SectionKey = 'profiles' | 'cameras' | 'models' | 'calibrations' | 'logs';

// Left navigation rail: Profiles, Cameras, Models, Calibrations, Logs, Settings. Data sections
// fetch lazily on first expand; Settings links to its own page.
export const LeftRail: React.FC<{ snapshot: StateSnapshot | null; onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ snapshot, onToast }) => {
  const [open, setOpen] = React.useState<SectionKey | null>('cameras');

  const toggle = (key: SectionKey) => setOpen(prev => (prev === key ? null : key));

  const cameraSources = React.useMemo(
    () => (snapshot?.Sources ?? []).filter(s => s.CameraHardwareInfo != null),
    [snapshot],
  );

  return (
    <div className="w-56 flex-shrink-0 h-full overflow-y-auto bg-white dark:bg-gray-800 border-r border-gray-200 dark:border-gray-700 text-sm">
      <RailSection title="Profiles" icon={Layers} open={open === 'profiles'} onToggle={() => toggle('profiles')}>
        <ProfilesSection onToast={onToast} />
      </RailSection>

      <RailSection title="Cameras" icon={Camera} open={open === 'cameras'} onToggle={() => toggle('cameras')}>
        {cameraSources.length === 0 ? (
          <EmptyNote text="No camera sources yet" />
        ) : (
          <ul className="space-y-1">
            {cameraSources.map(s => (
              <li key={s.Id} className="px-2 py-1 rounded hover:bg-gray-50 dark:hover:bg-gray-700">
                <p className="font-medium text-gray-800 dark:text-gray-200 truncate">{s.Name}</p>
                <p className="text-xs text-gray-500 dark:text-gray-400 truncate">{s.CameraHardwareInfo?.path}</p>
              </li>
            ))}
          </ul>
        )}
      </RailSection>

      <RailSection title="Models" icon={Box} open={open === 'models'} onToggle={() => toggle('models')}>
        <ModelsSection />
      </RailSection>

      <RailSection title="Calibrations" icon={Grid3x3} open={open === 'calibrations'} onToggle={() => toggle('calibrations')}>
        <CalibrationsSection />
      </RailSection>

      <RailSection title="Logs" icon={ScrollText} open={open === 'logs'} onToggle={() => toggle('logs')}>
        <LogsSection />
      </RailSection>

      <NavLink
        to="/recordings"
        className="flex items-center gap-2 px-3 py-2 border-t border-gray-200 dark:border-gray-700 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700"
      >
        <Film className="w-4 h-4" />Recordings
      </NavLink>

      <NavLink
        to="/settings"
        className="flex items-center gap-2 px-3 py-2 border-t border-gray-200 dark:border-gray-700 text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700"
      >
        <SettingsIcon className="w-4 h-4" />Settings
      </NavLink>
    </div>
  );
};

const RailSection: React.FC<{
  title: string;
  icon: React.ComponentType<{ className?: string }>;
  open: boolean;
  onToggle: () => void;
  children: React.ReactNode;
}> = ({ title, icon: Icon, open, onToggle, children }) => (
  <div className="border-b border-gray-100 dark:border-gray-700">
    <button
      onClick={onToggle}
      className="w-full flex items-center gap-2 px-3 py-2 text-left font-medium text-gray-700 dark:text-gray-300 hover:bg-gray-50 dark:hover:bg-gray-700"
    >
      {open ? <ChevronDown className="w-3.5 h-3.5" /> : <ChevronRight className="w-3.5 h-3.5" />}
      <Icon className="w-4 h-4" />{title}
    </button>
    {open && <div className="px-3 pb-3">{children}</div>}
  </div>
);

const EmptyNote: React.FC<{ text: string }> = ({ text }) => (
  <p className="text-xs text-gray-400 dark:text-gray-500 italic">{text}</p>
);

const ProfilesSection: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [profiles, setProfiles] = React.useState<string[] | null>(null);

  React.useEffect(() => {
    api.listGraphProfiles().then(setProfiles).catch(() => setProfiles([]));
  }, []);

  const activate = async (name: string) => {
    if (!window.confirm(`Replace the current graph with saved profile "${name}"? Anything not saved first will be lost.`)) return;
    try {
      await api.activateGraphProfile(name);
      onToast(`Activated graph profile "${name}"`, 'success');
    } catch {
      onToast('Failed to activate graph profile', 'error');
    }
  };

  if (profiles === null) return <EmptyNote text="Loading…" />;
  if (profiles.length === 0) return <EmptyNote text='No saved profiles yet - use "Save graph as…" in the toolbar above' />;
  return (
    <ul className="space-y-1">
      {profiles.map(name => (
        <li key={name} className="flex items-center justify-between gap-1 px-2 py-1 rounded hover:bg-gray-50 dark:hover:bg-gray-700">
          <span className="truncate text-gray-800 dark:text-gray-200">{name}</span>
          <button onClick={() => activate(name)} title="Activate this profile" className="p-1 text-purple-600 hover:text-purple-700 flex-shrink-0">
            <PlayCircle className="w-3.5 h-3.5" />
          </button>
        </li>
      ))}
    </ul>
  );
};

const ModelsSection: React.FC = () => {
  const [models, setModels] = React.useState<Model[] | null>(null);

  React.useEffect(() => {
    api.getAllModels().then(setModels).catch(() => setModels([]));
  }, []);

  if (models === null) return <EmptyNote text="Loading…" />;
  if (models.length === 0) return <EmptyNote text="No models uploaded yet" />;
  const VARIANT_NAMES = ['YOLOv8', 'YOLOv11'];
  return (
    <ul className="space-y-1">
      {models.map(m => (
        <li key={m.id} className="px-2 py-1 rounded hover:bg-gray-50 dark:hover:bg-gray-700">
          <p className="font-medium text-gray-800 dark:text-gray-200 truncate">{m.name}</p>
          <p className="text-xs text-gray-500 dark:text-gray-400">{VARIANT_NAMES[m.variant] ?? 'Unknown'} · {m.inputSize}px</p>
        </li>
      ))}
    </ul>
  );
};

const CalibrationsSection: React.FC = () => {
  const [calibrations, setCalibrations] = React.useState<StoredCameraCalibration[] | null>(null);

  React.useEffect(() => {
    api.getSavedCalibrations().then(setCalibrations).catch(() => setCalibrations([]));
  }, []);

  if (calibrations === null) return <EmptyNote text="Loading…" />;
  if (calibrations.length === 0) return <EmptyNote text="No saved calibrations yet" />;
  return (
    <ul className="space-y-1">
      <li className="px-2 py-1">
        <NavLink to="/calibration" className="text-xs text-blue-600 hover:text-blue-700">Open the Calibration tab</NavLink>
      </li>
      {calibrations.map((c, i) => (
        <li key={`${c.CameraPath}-${c.CalibratedAtUnixMs}-${i}`} className="px-2 py-1 rounded hover:bg-gray-50 dark:hover:bg-gray-700">
          <p className="font-medium text-gray-800 dark:text-gray-200 truncate">{c.CameraPath}</p>
          <p className="text-xs text-gray-500 dark:text-gray-400">
            {c.Result.ImageWidth}x{c.Result.ImageHeight} · rms {c.Result.Rms.toFixed(3)} · {new Date(c.CalibratedAtUnixMs).toLocaleDateString()}
          </p>
        </li>
      ))}
    </ul>
  );
};

const LogsSection: React.FC = () => {
  const [lines, setLines] = React.useState<string[] | null>(null);

  const refresh = React.useCallback(() => {
    api.getLogTail(100).then(setLines).catch(() => setLines([]));
  }, []);

  React.useEffect(() => { refresh(); }, [refresh]);

  return (
    <div>
      <button onClick={refresh} className="mb-2 text-xs text-blue-600 hover:text-blue-700 flex items-center gap-1">
        <RefreshCw className="w-3 h-3" />Refresh
      </button>
      {lines === null ? (
        <EmptyNote text="Loading…" />
      ) : (
        <pre className="text-[10px] leading-tight bg-gray-50 dark:bg-gray-900 text-gray-700 dark:text-gray-300 rounded p-2 max-h-64 overflow-y-auto whitespace-pre-wrap break-all">
          {lines.length === 0 ? '(no log entries)' : lines.join('\n')}
        </pre>
      )}
    </div>
  );
};
