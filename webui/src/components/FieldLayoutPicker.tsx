import React, { useEffect, useRef, useState } from 'react';
import { ApiService } from '../services/ApiService';
import type { FieldLayoutInfo } from '../types';

const api = new ApiService();

// Chooses the AprilTag field layout a detector or pipeline uses: one shipped with the server, or a WPILib layout JSON from disk. The
// caller supplies how to read the current tag count and how to apply each choice; the layout enables multi-tag poses.
export const FieldLayoutPicker: React.FC<{
  loadTagCount: () => Promise<number>;
  applyBundled: (layoutId: string) => Promise<number>;
  applyUpload: (json: string) => Promise<number>;
  onToast: (message: string, type: 'success' | 'error' | 'info') => void;
  // changes when the thing being edited does, so the tag count is read again
  resetKey: string | number;
}> = ({ loadTagCount, applyBundled, applyUpload, onToast, resetKey }) => {
  const [layouts, setLayouts] = useState<FieldLayoutInfo[]>([]);
  const [tagCount, setTagCount] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null);

  useEffect(() => {
    api.listFieldLayouts().then(setLayouts).catch(() => setLayouts([]));
  }, []);

  useEffect(() => {
    let cancelled = false;
    setTagCount(null);
    loadTagCount().then(count => { if (!cancelled) setTagCount(count); }).catch(() => { /* the count simply stays unknown */ });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [resetKey]);

  const run = async (action: () => Promise<number>) => {
    setBusy(true);
    try {
      const count = await action();
      setTagCount(count);
      onToast(count >= 0 ? `Field layout loaded (${count} tags)` : 'That is not a valid field layout', count >= 0 ? 'success' : 'error');
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to load the field layout', 'error');
    } finally {
      setBusy(false);
    }
  };

  const describe = () => {
    if (tagCount === null) return 'Reading...';
    if (tagCount < 0) return 'The saved layout is not valid';
    return tagCount === 0 ? 'No field layout (multi-tag and field poses off)' : `${tagCount} tags loaded`;
  };

  return (
    <div className="space-y-1">
      <label className="block text-xs text-gray-500 dark:text-gray-400">Field layout</label>
      <div className="flex gap-2">
        <select value="" disabled={busy} onChange={e => e.target.value && run(() => applyBundled(e.target.value))}
          className="flex-1 px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white disabled:opacity-50">
          <option value="">Choose a bundled layout...</option>
          {layouts.map(l => <option key={l.Id} value={l.Id}>{l.Name} ({l.TagCount} tags)</option>)}
        </select>
        <button onClick={() => fileInput.current?.click()} disabled={busy}
          className="px-2 py-1 bg-gray-200 dark:bg-gray-700 rounded text-xs disabled:opacity-50">Upload JSON</button>
        <input ref={fileInput} type="file" accept=".json,application/json" className="hidden"
          onChange={async e => {
            const file = e.target.files?.[0];
            e.target.value = '';
            if (file) await run(async () => applyUpload(await file.text()));
          }} />
      </div>
      <div className="text-xs text-gray-500 dark:text-gray-400">{describe()}</div>
    </div>
  );
};
