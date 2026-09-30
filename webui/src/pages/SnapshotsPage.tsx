import React, { useEffect, useState } from 'react';
import { Camera, Download, Trash2, RefreshCw } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import type { SnapshotEntry } from '../types';

const api = new ApiService();

// Every saved snapshot (taken from a camera's Inspector panel or by a robot's NT snapshot request), grouped by camera.
export const SnapshotsPage: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [snapshots, setSnapshots] = useState<SnapshotEntry[]>([]);
  const [loading, setLoading] = useState(true);

  const refresh = async () => {
    try {
      setSnapshots(await api.listSnapshots());
    } catch {
      onToast('Failed to load snapshots', 'error');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const remove = async (entry: SnapshotEntry) => {
    try {
      await api.deleteSnapshot(entry.Path);
      setSnapshots(current => current.filter(s => s.Path !== entry.Path));
    } catch {
      onToast('Failed to delete the snapshot', 'error');
    }
  };

  const cameras = Array.from(new Set(snapshots.map(s => s.Camera)));

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Camera className="w-6 h-6" />Snapshots</h1>
        <button onClick={refresh} className="px-3 py-1.5 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-sm flex items-center gap-1">
          <RefreshCw className="w-3.5 h-3.5" />Refresh
        </button>
      </div>

      {!loading && snapshots.length === 0 && (
        <p className="text-gray-500 dark:text-gray-400">No snapshots yet - take one from a camera node's Inspector panel, or from robot code with PhotonCamera.takeInputSnapshot().</p>
      )}

      {cameras.map(camera => (
        <div key={camera} className="bg-white dark:bg-gray-800 rounded-lg shadow p-4">
          <h2 className="font-semibold text-gray-900 dark:text-white mb-3">{camera || 'Uncategorised'}</h2>
          <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-4 gap-3">
            {snapshots.filter(s => s.Camera === camera).map(entry => (
              <div key={entry.Path} className="bg-gray-50 dark:bg-gray-700 rounded overflow-hidden text-xs">
                <a href={api.getSnapshotUrl(entry.Path)} target="_blank" rel="noreferrer">
                  <img src={api.getSnapshotUrl(entry.Path)} alt={entry.Path} loading="lazy" className="w-full aspect-video object-cover bg-black" />
                </a>
                <div className="p-2 flex items-center justify-between gap-2">
                  <div className="min-w-0">
                    <div className="truncate" title={entry.Path}>{new Date(entry.CreatedUtc).toLocaleString()}</div>
                    <div className="text-gray-500 dark:text-gray-400">{entry.Kind} - {(entry.SizeBytes / 1024).toFixed(0)} KB</div>
                  </div>
                  <a href={api.getSnapshotUrl(entry.Path)} download={entry.Path.split('/').pop()} className="text-blue-600 hover:text-blue-700" title="Download">
                    <Download className="w-4 h-4" />
                  </a>
                  <button onClick={() => remove(entry)} className="text-red-600 hover:text-red-700" title="Delete">
                    <Trash2 className="w-4 h-4" />
                  </button>
                </div>
              </div>
            ))}
          </div>
        </div>
      ))}
    </div>
  );
};
