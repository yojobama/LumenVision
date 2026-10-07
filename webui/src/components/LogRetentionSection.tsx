import React, { useCallback, useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import { SettingsSection } from './SettingsSection';
import type { DeviceLogSettings, LogUsage } from '../types';

const api = new ApiService();
const field = 'w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white';

const megabytes = (bytes: number) => `${(bytes / (1024 * 1024)).toFixed(bytes < 10 * 1024 * 1024 ? 1 : 0)} MB`;

// How much log history to keep (the numbers are saved with the rest of the settings), how much disk the logs use now, and a way to erase them.
export const LogRetentionSection: React.FC<{
  logs: DeviceLogSettings;
  onChange: (logs: DeviceLogSettings) => void;
  onToast: (m: string, t: 'success' | 'error' | 'info') => void;
}> = ({ logs, onChange, onToast }) => {
  const [usage, setUsage] = useState<LogUsage | null>(null);

  const refresh = useCallback(() => {
    api.getLogUsage().then(setUsage).catch(() => setUsage(null));
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const clear = async () => {
    if (!confirm('Erase every log on this device? Rotated copies are deleted and the current files are emptied.')) return;
    try {
      const count = await api.clearLogs();
      onToast(`Cleared ${count} log files`, 'success');
      refresh();
    } catch {
      onToast('Failed to clear the logs', 'error');
    }
  };

  const number = (label: string, key: keyof DeviceLogSettings, min: number, max: number, hint: string) => (
    <label className="text-xs text-gray-500 dark:text-gray-400" title={hint}>
      {label}
      <input type="number" min={min} max={max} value={logs[key]} className={field}
        onChange={e => onChange({ ...logs, [key]: Math.min(max, Math.max(min, parseInt(e.target.value) || min)) })} />
    </label>
  );

  return (
    <SettingsSection title="Logs" hint="Every log call is kept; what is limited is how much history stays on disk. Saved with the other settings above.">
      <div className="grid grid-cols-2 gap-2">
        {number('File size (MB)', 'MaxFileMb', 1, 1024, 'A log file is rotated once it reaches this size.')}
        {number('Rotated copies kept', 'FilesKept', 0, 20, 'How many older copies of LumenCore\'s and the store\'s logs are kept next to the live file.')}
        {number('Server log budget (MB)', 'ServerBudgetMb', 1, 10240, 'Everything under logs/ together; the oldest files go first.')}
        {number('Keep server logs (days)', 'KeepDays', 1, 365, 'Server log files older than this are deleted.')}
      </div>
      <div className="flex items-center justify-between text-sm text-gray-700 dark:text-gray-300">
        <span>
          {usage ? `${megabytes(usage.TotalBytes)} in ${usage.Files} files (LumenCore ${megabytes(usage.CoreBytes)}, server ${megabytes(usage.ServerBytes)}, store ${megabytes(usage.DatabaseBytes)})` : 'Reading disk use...'}
        </span>
        <span className="flex gap-2">
          <button onClick={refresh} className="px-2 py-1 bg-gray-200 dark:bg-gray-700 rounded text-xs">Refresh</button>
          <button onClick={clear} className="px-2 py-1 bg-red-600 text-white rounded text-xs hover:bg-red-700">Clear logs</button>
        </span>
      </div>
    </SettingsSection>
  );
};
