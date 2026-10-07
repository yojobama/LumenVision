import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Download, Pause, Play, Trash2, X } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import type { LogEntry } from '../types';

const api = new ApiService();

const LEVELS = ['debug', 'info', 'warning', 'error'] as const;
const LEVEL_STYLE = ['text-gray-400', 'text-gray-700 dark:text-gray-300', 'text-amber-600 dark:text-amber-400', 'text-red-600 dark:text-red-400'];
const MAX_ENTRIES = 2000;

// The device's logs, live: toggled with the backtick key (or the Logs button). Shows the recent history and then follows /ws/logs.
export const LogDrawer: React.FC<{ open: boolean; onClose: () => void }> = ({ open, onClose }) => {
  const [entries, setEntries] = useState<LogEntry[]>([]);
  const [level, setLevel] = useState<(typeof LEVELS)[number]>('info');
  const [filter, setFilter] = useState('');
  const [paused, setPaused] = useState(false);
  const pausedRef = useRef(false);
  const scroller = useRef<HTMLDivElement>(null);
  pausedRef.current = paused;

  // history first, then the live feed; both restart when the level changes
  useEffect(() => {
    if (!open) return;
    let cancelled = false;
    let socket: WebSocket | undefined;
    let reconnect: ReturnType<typeof setTimeout> | undefined;

    const follow = () => {
      if (cancelled) return;
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(`${protocol}//${window.location.host}/ws/logs?level=${level}`);
      socket.onmessage = event => {
        if (pausedRef.current) return;
        try {
          const entry: LogEntry = JSON.parse(event.data);
          setEntries(current => [...current.slice(-(MAX_ENTRIES - 1)), entry]);
        } catch { /* a malformed message is skipped */ }
      };
      socket.onclose = () => { if (!cancelled) reconnect = setTimeout(follow, 2000); };
      socket.onerror = () => socket?.close();
    };

    api.getLogEntries(300, level)
      .then(history => { if (!cancelled) setEntries(history); })
      .catch(() => { if (!cancelled) setEntries([]); })
      .finally(follow);

    return () => {
      cancelled = true;
      clearTimeout(reconnect);
      socket?.close();
    };
  }, [open, level]);

  const shown = useMemo(() => {
    const text = filter.trim().toLowerCase();
    return text ? entries.filter(e => e.Message.toLowerCase().includes(text) || e.Source.includes(text)) : entries;
  }, [entries, filter]);

  // stick to the bottom while following
  useEffect(() => {
    if (!paused && scroller.current) scroller.current.scrollTop = scroller.current.scrollHeight;
  }, [shown, paused]);

  if (!open) return null;

  return (
    <div className="fixed inset-x-0 bottom-0 z-40 h-1/2 bg-white dark:bg-gray-900 border-t border-gray-300 dark:border-gray-600 shadow-2xl flex flex-col">
      <div className="flex items-center gap-2 px-3 py-2 border-b border-gray-200 dark:border-gray-700 text-sm">
        <span className="font-semibold text-gray-900 dark:text-white">Logs</span>
        <select value={level} onChange={e => setLevel(e.target.value as (typeof LEVELS)[number])} aria-label="Minimum level"
          className="px-2 py-1 border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-800 dark:text-white">
          {LEVELS.map(l => <option key={l} value={l}>{l === 'debug' ? 'everything' : `${l} and up`}</option>)}
        </select>
        <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="Filter text..."
          className="flex-1 max-w-xs px-2 py-1 border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-800 dark:text-white" />
        <button onClick={() => setPaused(p => !p)} title={paused ? 'Resume' : 'Pause'} className="p-1.5 rounded bg-gray-200 dark:bg-gray-700">
          {paused ? <Play className="w-4 h-4" /> : <Pause className="w-4 h-4" />}
        </button>
        <button onClick={() => setEntries([])} title="Clear this view" className="p-1.5 rounded bg-gray-200 dark:bg-gray-700"><Trash2 className="w-4 h-4" /></button>
        <a href={api.getLogDownloadUrl()} download title="Download every log file" className="p-1.5 rounded bg-gray-200 dark:bg-gray-700"><Download className="w-4 h-4" /></a>
        <span className="flex-1" />
        <span className="text-xs text-gray-500 dark:text-gray-400">{shown.length} lines - press ` to close</span>
        <button onClick={onClose} title="Close" className="p-1.5"><X className="w-4 h-4" /></button>
      </div>
      <div ref={scroller} className="flex-1 overflow-y-auto font-mono text-xs px-3 py-2 space-y-0.5">
        {shown.length === 0 && <div className="text-gray-400">Nothing logged at this level yet.</div>}
        {shown.map(entry => (
          <div key={entry.Id} className={`whitespace-pre-wrap break-words ${LEVEL_STYLE[entry.Level] ?? ''}`}>
            <span className="text-gray-400">{new Date(entry.TimeUtc).toLocaleTimeString()}</span> <span className="text-gray-400">[{entry.Source}]</span> {entry.Message}
          </div>
        ))}
      </div>
    </div>
  );
};
