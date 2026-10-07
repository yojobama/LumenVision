import React, { useRef, useState } from 'react';
import { ApiService } from '../services/ApiService';
import { SettingsSection } from './SettingsSection';

const api = new ApiService();

// Settings backup: download everything that configures the device, or restore it from such a file (on this or another coprocessor).
export const DataSection: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [models, setModels] = useState(false);
  const [log, setLog] = useState(false);
  const [importing, setImporting] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null);

  const importFile = async (file: File) => {
    if (!confirm(`Replace this device's pipeline graph, settings and calibrations with the contents of "${file.name}"?\n\nThe current ones are saved to the backups folder first, and the server restarts.`)) return;
    setImporting(true);
    try {
      const result = await api.importSettings(file);
      onToast(`Restored ${result.FilesRestored} files - the server is restarting...`, 'success');
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'The import failed', 'error');
    } finally {
      setImporting(false);
    }
  };

  return (
    <SettingsSection title="Backup and restore" hint="One ZIP with the pipeline graph, settings, saved graph profiles, uploaded field layouts and camera calibrations.">
      <div className="space-y-2 text-sm text-gray-700 dark:text-gray-300">
        <label className="flex items-center gap-2"><input type="checkbox" checked={models} onChange={e => setModels(e.target.checked)} />Include uploaded models (can be large)</label>
        <label className="flex items-center gap-2"><input type="checkbox" checked={log} onChange={e => setLog(e.target.checked)} />Include the server log</label>
      </div>
      <div className="flex gap-2">
        <a href={api.getSettingsExportUrl(models, log)} className="px-3 py-2 bg-blue-600 text-white rounded text-sm hover:bg-blue-700" download>Download settings</a>
        <button onClick={() => fileInput.current?.click()} disabled={importing} className="px-3 py-2 bg-gray-700 text-white rounded text-sm hover:bg-gray-800 disabled:opacity-50">
          {importing ? 'Restoring...' : 'Restore from file...'}
        </button>
        <input ref={fileInput} type="file" accept=".zip,application/zip" className="hidden"
          onChange={e => { const file = e.target.files?.[0]; e.target.value = ''; if (file) importFile(file); }} />
      </div>
    </SettingsSection>
  );
};
