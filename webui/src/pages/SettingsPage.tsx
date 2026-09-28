import React, { useState, useEffect } from 'react';
import { CheckCircle, Settings as SettingsIcon } from 'lucide-react';
import type { Settings as SettingsType } from '../types';

// Settings form, routed at /settings.
export const SettingsPage: React.FC<{
  settings: SettingsType;
  onSave: (settings: SettingsType) => void;
}> = ({ settings, onSave }) => {
  const [localSettings, setLocalSettings] = useState(settings);
  useEffect(() => setLocalSettings(settings), [settings]);

  return (
    <div className="max-w-2xl space-y-6">
      <div>
        <h2 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><SettingsIcon className="w-6 h-6" />Settings</h2>
        <p className="text-gray-600 dark:text-gray-400">Coprocessor connection and NetworkTables defaults</p>
      </div>
      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6 space-y-4">
        <div>
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Server URL</label>
          <input
            type="text"
            value={localSettings.serverUrl}
            onChange={(e) => setLocalSettings({...localSettings, serverUrl: e.target.value})}
            className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
          />
        </div>
        <div className="pt-2 border-t border-gray-200 dark:border-gray-700">
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">NetworkTables Connection</label>
          <p className="text-xs text-gray-500 dark:text-gray-400 mb-2">Used by every node's "Publish to NT4" toggle - one robot only has one NT4 server to talk to.</p>
          <div className="flex gap-4 text-sm mb-2">
            <label className="flex items-center gap-1">
              <input type="radio" checked={localSettings.nt4.mode === 'team'} onChange={() => setLocalSettings({...localSettings, nt4: {...localSettings.nt4, mode: 'team'}})} />Team Number
            </label>
            <label className="flex items-center gap-1">
              <input type="radio" checked={localSettings.nt4.mode === 'server'} onChange={() => setLocalSettings({...localSettings, nt4: {...localSettings.nt4, mode: 'server'}})} />Server Address
            </label>
          </div>
          {localSettings.nt4.mode === 'team' ? (
            <input
              type="number"
              value={localSettings.nt4.teamNumber ?? ''}
              onChange={(e) => setLocalSettings({...localSettings, nt4: {...localSettings.nt4, teamNumber: e.target.value === '' ? undefined : parseInt(e.target.value)}})}
              placeholder="FRC team number, e.g. 1234"
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white mb-2"
            />
          ) : (
            <div className="flex gap-2 mb-2">
              <input
                type="text"
                value={localSettings.nt4.serverAddress ?? ''}
                onChange={(e) => setLocalSettings({...localSettings, nt4: {...localSettings.nt4, serverAddress: e.target.value}})}
                placeholder="Server address, e.g. 10.0.0.2"
                className="flex-1 px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
              />
              <input
                type="number"
                value={localSettings.nt4.port ?? ''}
                onChange={(e) => setLocalSettings({...localSettings, nt4: {...localSettings.nt4, port: e.target.value === '' ? undefined : parseInt(e.target.value)}})}
                placeholder="Port (default)"
                className="w-28 px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
              />
            </div>
          )}
          <input
            type="text"
            value={localSettings.nt4.rootTable}
            onChange={(e) => setLocalSettings({...localSettings, nt4: {...localSettings.nt4, rootTable: e.target.value}})}
            placeholder="Root table"
            className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
          />
        </div>
        <div className="flex justify-end pt-2">
          <button onClick={() => onSave(localSettings)} className="px-4 py-2 bg-blue-600 text-white rounded hover:bg-blue-700 flex items-center gap-2">
            <CheckCircle className="w-4 h-4" />Save
          </button>
        </div>
      </div>
    </div>
  );
};
