import React, { useEffect, useState } from 'react';
import { CheckCircle, Settings as SettingsIcon } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import type { DeviceSettingsData } from '../types';
import { NetworkSection } from '../components/NetworkSection';
import { SettingsSection } from '../components/SettingsSection';

const api = new ApiService();

const field = 'w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white';

// Device settings, routed at /settings. Everything here lives on the coprocessor (settings.json), so every browser sees the same values.
export const SettingsPage: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [settings, setSettings] = useState<DeviceSettingsData | null>(null);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    api.getDeviceSettings().then(setSettings).catch(() => onToast('Failed to load the device settings', 'error'));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const save = async () => {
    if (!settings) return;
    setSaving(true);
    try {
      setSettings(await api.putDeviceSettings(settings));
      onToast('Settings saved', 'success');
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to save the settings', 'error');
    } finally {
      setSaving(false);
    }
  };

  const nt = settings?.NetworkTables;
  const led = settings?.Led;

  return (
    <div className="max-w-2xl space-y-6">
      <div>
        <h2 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><SettingsIcon className="w-6 h-6" />Settings</h2>
        <p className="text-gray-600 dark:text-gray-400">Stored on the coprocessor, so every browser sees the same values</p>
      </div>

      <NetworkSection onToast={onToast} />

      {settings && nt && led && (
        <>
          <SettingsSection title="NetworkTables connection"
            hint="Used by every node's &quot;Publish to NT4&quot; toggle - a robot has one NT4 server. Changing it reconnects the nodes already publishing.">
            <div className="flex gap-4 text-sm">
              <label className="flex items-center gap-1">
                <input type="radio" checked={nt.Mode === 'team'} onChange={() => setSettings({ ...settings, NetworkTables: { ...nt, Mode: 'team' } })} />Team number
              </label>
              <label className="flex items-center gap-1">
                <input type="radio" checked={nt.Mode === 'server'} onChange={() => setSettings({ ...settings, NetworkTables: { ...nt, Mode: 'server' } })} />Server address
              </label>
            </div>
            {nt.Mode === 'team' ? (
              <input type="number" value={nt.TeamNumber ?? ''} placeholder="FRC team number, e.g. 1234" className={field}
                onChange={e => setSettings({ ...settings, NetworkTables: { ...nt, TeamNumber: e.target.value === '' ? null : parseInt(e.target.value) } })} />
            ) : (
              <div className="flex gap-2">
                <input type="text" value={nt.ServerAddress ?? ''} placeholder="Server address, e.g. 10.0.0.2" className={`flex-1 ${field}`}
                  onChange={e => setSettings({ ...settings, NetworkTables: { ...nt, ServerAddress: e.target.value } })} />
                <input type="number" value={nt.Port || ''} placeholder="Port (default)" className="w-28 px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
                  onChange={e => setSettings({ ...settings, NetworkTables: { ...nt, Port: e.target.value === '' ? 0 : parseInt(e.target.value) } })} />
              </div>
            )}
            <div className="grid grid-cols-2 gap-2">
              <label className="text-xs text-gray-500 dark:text-gray-400">Root table
                <input type="text" value={nt.RootTable} className={field} onChange={e => setSettings({ ...settings, NetworkTables: { ...nt, RootTable: e.target.value } })} />
              </label>
              <label className="text-xs text-gray-500 dark:text-gray-400">Client identity
                <input type="text" value={nt.ClientIdentity} className={field} onChange={e => setSettings({ ...settings, NetworkTables: { ...nt, ClientIdentity: e.target.value } })} />
              </label>
            </div>
          </SettingsSection>

          <SettingsSection title="LED ring" hint="The GPIO line that lights the camera's LED ring, driven by the robot's LED commands. Leave off if there is none.">
            <label className="flex items-center gap-2 text-sm text-gray-700 dark:text-gray-300">
              <input type="checkbox" checked={led.Enabled} onChange={e => setSettings({ ...settings, Led: { ...led, Enabled: e.target.checked } })} />
              Drive an LED from a GPIO line
            </label>
            {led.Enabled && (
              <div className="grid grid-cols-3 gap-2">
                <label className="text-xs text-gray-500 dark:text-gray-400">GPIO chip
                  <input type="number" min={0} value={led.Chip} className={field} onChange={e => setSettings({ ...settings, Led: { ...led, Chip: parseInt(e.target.value) || 0 } })} />
                </label>
                <label className="text-xs text-gray-500 dark:text-gray-400">Line
                  <input type="number" min={0} value={led.Line} className={field} onChange={e => setSettings({ ...settings, Led: { ...led, Line: parseInt(e.target.value) || 0 } })} />
                </label>
                <label className="flex items-end gap-2 text-xs text-gray-700 dark:text-gray-300 pb-2">
                  <input type="checkbox" checked={led.ActiveLow} onChange={e => setSettings({ ...settings, Led: { ...led, ActiveLow: e.target.checked } })} />Active low
                </label>
              </div>
            )}
          </SettingsSection>

          <div className="flex justify-end">
            <button onClick={save} disabled={saving} className="px-4 py-2 bg-blue-600 text-white rounded hover:bg-blue-700 disabled:opacity-50 flex items-center gap-2">
              <CheckCircle className="w-4 h-4" />Save
            </button>
          </div>
        </>
      )}
    </div>
  );
};
