import React, { useState } from 'react';
import { ApiService } from '../services/ApiService';
import { SettingsSection } from './SettingsSection';

const api = new ApiService();
const button = 'px-3 py-2 rounded text-sm text-white disabled:opacity-50';

// Restart the server, reboot or power off the board, or wipe the saved data. The page loses its connection while the device restarts and reconnects
// by itself.
export const PowerSection: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [resetOpen, setResetOpen] = useState(false);
  const [phrase, setPhrase] = useState('');
  const [keep, setKeep] = useState({ calibrations: true, models: true, media: false });

  const run = async (label: string, action: () => Promise<unknown>, success: string) => {
    try {
      await action();
      onToast(success, 'info');
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : `${label} failed`, 'error');
    }
  };

  const confirmThen = (question: string, label: string, action: 'restart' | 'reboot' | 'shutdown', success: string) => () => {
    if (confirm(question)) run(label, () => api.powerAction(action), success);
  };

  return (
    <SettingsSection title="Device" hint="Restarting the server keeps the board up; a reboot or shutdown stops every camera. Shutting down needs someone to switch the board on again.">
      <div className="flex flex-wrap gap-2">
        <button className={`${button} bg-blue-600 hover:bg-blue-700`}
          onClick={confirmThen('Restart the LumenVision server? Cameras and streams stop for a few seconds.', 'Restart', 'restart', 'Restarting the server...')}>Restart server</button>
        <button className={`${button} bg-amber-600 hover:bg-amber-700`}
          onClick={confirmThen('Reboot the board? This takes about a minute.', 'Reboot', 'reboot', 'Rebooting...')}>Reboot</button>
        <button className={`${button} bg-red-600 hover:bg-red-700`}
          onClick={confirmThen('Shut the board down? It will not come back until it is powered on again.', 'Shutdown', 'shutdown', 'Shutting down...')}>Shut down</button>
        <button className={`${button} bg-gray-700 hover:bg-gray-800`} onClick={() => setResetOpen(open => !open)}>Factory reset...</button>
      </div>

      {resetOpen && (
        <div className="border border-red-300 dark:border-red-800 rounded-md p-3 space-y-3 text-sm text-gray-800 dark:text-gray-200">
          <p>Deletes the pipeline graph, settings, saved graph profiles and uploaded field layouts, then restarts the server. Choose what else to keep:</p>
          <label className="flex items-center gap-2"><input type="checkbox" checked={keep.calibrations} onChange={e => setKeep({ ...keep, calibrations: e.target.checked })} />Camera calibrations</label>
          <label className="flex items-center gap-2"><input type="checkbox" checked={keep.models} onChange={e => setKeep({ ...keep, models: e.target.checked })} />Object detection models</label>
          <label className="flex items-center gap-2"><input type="checkbox" checked={keep.media} onChange={e => setKeep({ ...keep, media: e.target.checked })} />Recordings, snapshots, images and videos</label>
          <input value={phrase} onChange={e => setPhrase(e.target.value)} placeholder='Type "factory reset" to confirm'
            className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white" />
          <button disabled={phrase.trim().toLowerCase() !== 'factory reset'} className={`${button} bg-red-600 hover:bg-red-700`}
            onClick={() => run('Factory reset', () => api.factoryReset(phrase, keep), 'Resetting - the server is restarting...')}>Erase and restart</button>
        </div>
      )}
    </SettingsSection>
  );
};
