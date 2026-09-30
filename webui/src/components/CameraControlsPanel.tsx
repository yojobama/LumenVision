import React, { useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import { CameraControlKind } from '../types';
import type { CameraControl } from '../types';
import { ToggleSwitch } from './ToggleSwitch';

const api = new ApiService();

// Every control the camera device reports (brightness, contrast, white balance, ...), rendered by kind. Changes apply immediately and are
// remembered by the server; a control that changes what others allow (auto exposure vs exposure time) triggers a re-read of the list.
export const CameraControlsPanel: React.FC<{
  sourceId: number;
  onToast: (message: string, type: 'success' | 'error' | 'info') => void;
}> = ({ sourceId, onToast }) => {
  const [controls, setControls] = useState<CameraControl[] | null>(null);

  const load = () => api.getCameraControlList(sourceId).then(setControls).catch(() => setControls([]));

  useEffect(() => {
    setControls(null);
    let cancelled = false;
    api.getCameraControlList(sourceId)
      .then(list => { if (!cancelled) setControls(list); })
      .catch(() => { if (!cancelled) setControls([]); });
    return () => { cancelled = true; };
  }, [sourceId]);

  const write = async (control: CameraControl, value: number) => {
    setControls(current => current?.map(c => (c.Id === control.Id ? { ...c, Value: value } : c)) ?? null);
    try {
      const ok = await api.setCameraControl(sourceId, control.Id, value);
      if (!ok) onToast(`The camera rejected ${control.Name} = ${value}`, 'error');
      await load();
    } catch {
      onToast(`Failed to set ${control.Name}`, 'error');
    }
  };

  if (controls === null) return <p className="text-xs text-gray-400">Reading camera controls...</p>;
  if (controls.length === 0) return <p className="text-xs text-gray-400">This camera does not report any adjustable controls.</p>;

  const inputClass = 'flex-1 px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white disabled:opacity-50';

  return (
    <div className="space-y-3">
      {controls.map(control => {
        const disabled = control.ReadOnly || control.Inactive;
        return (
          <div key={control.Id} title={control.Inactive ? 'Unavailable with the current settings (for example, manual exposure time while auto exposure is on)' : undefined}>
            {control.Kind === CameraControlKind.Integer && (
              <>
                <label className="flex justify-between text-xs text-gray-500 dark:text-gray-400 mb-1">
                  <span>{control.Name}</span>
                  <span>{control.Value} ({control.Minimum} to {control.Maximum})</span>
                </label>
                <div className="flex gap-2 items-center">
                  <input type="range" min={control.Minimum} max={control.Maximum} step={control.Step} value={control.Value} disabled={disabled}
                    onChange={e => setControls(current => current?.map(c => (c.Id === control.Id ? { ...c, Value: parseInt(e.target.value) } : c)) ?? null)}
                    onPointerUp={e => write(control, parseInt((e.target as HTMLInputElement).value))}
                    onKeyUp={e => write(control, parseInt((e.target as HTMLInputElement).value))}
                    className="flex-1" />
                  <button onClick={() => write(control, control.Default)} disabled={disabled} title="Reset to the device default"
                    className="px-2 py-0.5 text-xs bg-gray-200 dark:bg-gray-700 rounded disabled:opacity-50">Reset</button>
                </div>
              </>
            )}
            {control.Kind === CameraControlKind.Boolean && (
              <div className="flex items-center justify-between">
                <span className="text-xs text-gray-700 dark:text-gray-300">{control.Name}</span>
                <ToggleSwitch enabled={control.Value !== 0} onChange={enabled => write(control, enabled ? 1 : 0)} />
              </div>
            )}
            {control.Kind === CameraControlKind.Menu && (
              <>
                <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">{control.Name}</label>
                <select value={control.Value} disabled={disabled} onChange={e => write(control, parseInt(e.target.value))} className={`w-full ${inputClass}`}>
                  {control.MenuValues.map((value, index) => <option key={value} value={value}>{control.MenuLabels[index]}</option>)}
                </select>
              </>
            )}
            {control.Kind === CameraControlKind.Button && (
              <button onClick={() => write(control, 1)} disabled={disabled}
                className="w-full px-2 py-1 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-xs disabled:opacity-50">
                {control.Name}
              </button>
            )}
          </div>
        );
      })}
    </div>
  );
};
