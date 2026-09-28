import React from 'react';
import { Save, PlayCircle } from 'lucide-react';
import { ApiService } from '../services/ApiService';

const api = new ApiService();

// Save/activate controls for the whole node graph (distinct from per-source pipeline profiles).
export const GraphProfileBar: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [profiles, setProfiles] = React.useState<string[]>([]);
  const [selected, setSelected] = React.useState('');
  const [newName, setNewName] = React.useState('');
  const [showSaveInput, setShowSaveInput] = React.useState(false);
  const [busy, setBusy] = React.useState(false);

  const refresh = React.useCallback(() => {
    api.listGraphProfiles()
      .then(list => {
        setProfiles(list);
        setSelected(prev => (list.includes(prev) ? prev : (list[0] ?? '')));
      })
      .catch(() => { /* transient - the dropdown just stays at whatever it last had */ });
  }, []);

  React.useEffect(() => { refresh(); }, [refresh]);

  const saveAs = async () => {
    const name = newName.trim();
    if (!name) return;
    setBusy(true);
    try {
      await api.saveGraphProfileAs(name);
      onToast(`Saved graph profile "${name}"`, 'success');
      setNewName('');
      setShowSaveInput(false);
      refresh();
    } catch {
      onToast('Failed to save graph profile', 'error');
    } finally {
      setBusy(false);
    }
  };

  const activate = async () => {
    if (!selected) return;
    // destructive: tears down every live source/sink and rebuilds from the snapshot, so confirm first
    if (!window.confirm(`Replace the current graph with saved profile "${selected}"? Anything not saved first will be lost.`)) return;
    setBusy(true);
    try {
      await api.activateGraphProfile(selected);
      onToast(`Activated graph profile "${selected}"`, 'success');
    } catch {
      onToast('Failed to activate graph profile', 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="flex items-center gap-1.5 px-2 py-1 bg-white dark:bg-gray-800 rounded shadow text-sm">
      {profiles.length > 0 && (
        <>
          <select
            value={selected}
            onChange={e => setSelected(e.target.value)}
            className="px-1.5 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white"
          >
            {profiles.map(p => <option key={p} value={p}>{p}</option>)}
          </select>
          <button
            onClick={activate}
            disabled={busy || !selected}
            title="Replace the current graph with this saved profile"
            className="p-1.5 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-50"
          >
            <PlayCircle className="w-4 h-4 text-purple-600" />
          </button>
        </>
      )}

      {showSaveInput ? (
        <>
          <input
            autoFocus
            value={newName}
            onChange={e => setNewName(e.target.value)}
            onKeyDown={e => { if (e.key === 'Enter') saveAs(); if (e.key === 'Escape') setShowSaveInput(false); }}
            placeholder="profile name"
            className="px-1.5 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white w-28"
          />
          <button onClick={saveAs} disabled={busy || !newName.trim()} className="p-1.5 rounded hover:bg-gray-100 dark:hover:bg-gray-700 disabled:opacity-50">
            <Save className="w-4 h-4 text-purple-600" />
          </button>
        </>
      ) : (
        <button
          onClick={() => setShowSaveInput(true)}
          title="Save the current graph as a new profile"
          className="p-1.5 rounded hover:bg-gray-100 dark:hover:bg-gray-700 flex items-center gap-1 text-xs text-gray-600 dark:text-gray-300"
        >
          <Save className="w-4 h-4" />Save graph as...
        </button>
      )}
    </div>
  );
};
