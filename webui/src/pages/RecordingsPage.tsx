import React, { useEffect, useState } from 'react';
import { Film, Download, FolderInput, Trash2, RefreshCw } from 'lucide-react';
import { useStateSocket } from '../hooks/useStateSocket';
import { ApiService } from '../services/ApiService';
import { sinkTypeName } from '../graph/model';
import type { RecordSegment } from '../types';

const api = new ApiService();

// Every RecordSink's segments in one place: snapshot from /ws/state, segment listing per sink
// via REST.
export const RecordingsPage: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const { snapshot, connected } = useStateSocket();
  const [segmentsBySink, setSegmentsBySink] = useState<Record<number, RecordSegment[]>>({});
  const [promoting, setPromoting] = useState<string | null>(null);

  const recordSinks = (snapshot?.Sinks ?? []).filter(s => sinkTypeName(s.Sink.Type) === 'RecordSink');
  const recordSinkIds = recordSinks.map(s => s.Sink.Id).join(',');

  const refresh = async () => {
    const entries = await Promise.all(recordSinks.map(async s => {
      try {
        return [s.Sink.Id, await api.getRecordSinkSegments(s.Sink.Id)] as const;
      } catch {
        return [s.Sink.Id, [] as RecordSegment[]] as const;
      }
    }));
    setSegmentsBySink(Object.fromEntries(entries));
  };

  useEffect(() => {
    refresh();
    // re-fetch when the RecordSinks or their running states change
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [recordSinkIds, recordSinks.map(s => s.IsRunning).join(',')]);

  const promoteSegment = async (sinkId: number, fileName: string) => {
    setPromoting(`${sinkId}:${fileName}`);
    try {
      await api.promoteRecordSinkSegment(sinkId, fileName);
      onToast(`"${fileName}" is now available as a Video File source`, 'success');
    } catch {
      onToast(`Failed to use "${fileName}" as a source`, 'error');
    } finally {
      setPromoting(null);
    }
  };

  const deleteSegment = async (sinkId: number, fileName: string) => {
    try {
      await api.deleteRecordSinkSegment(sinkId, fileName);
      await refresh();
    } catch {
      onToast(`Failed to delete "${fileName}"`, 'error');
    }
  };

  if (!snapshot) {
    return (
      <div className="flex flex-col items-center justify-center py-24 text-center">
        <Film className="w-16 h-16 mb-4 text-gray-400" />
        <h2 className="text-xl font-semibold text-gray-900 dark:text-white mb-2">Recordings</h2>
        <p className="text-gray-600 dark:text-gray-400">{connected ? 'Waiting for the first state snapshot…' : 'Connecting to the coprocessor…'}</p>
      </div>
    );
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Film className="w-6 h-6" />Recordings</h1>
        <button onClick={refresh} className="px-3 py-1.5 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-sm flex items-center gap-1">
          <RefreshCw className="w-3.5 h-3.5" />Refresh
        </button>
      </div>

      {recordSinks.length === 0 && (
        <p className="text-gray-500 dark:text-gray-400">No recording sinks yet - start one from a camera or detector node's Inspector panel in the Graph editor.</p>
      )}

      {recordSinks.map(s => {
        const segments = segmentsBySink[s.Sink.Id] ?? [];
        return (
          <div key={s.Sink.Id} className="bg-white dark:bg-gray-800 rounded-lg shadow p-4">
            <div className="flex items-center justify-between mb-2">
              <h2 className="font-semibold text-gray-900 dark:text-white">{s.Sink.Name}</h2>
              <span className={`text-xs px-2 py-0.5 rounded ${s.IsRunning ? 'bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200' : 'bg-gray-100 text-gray-500 dark:bg-gray-700 dark:text-gray-400'}`}>
                {s.IsRunning ? 'Recording' : 'Stopped'}
              </span>
            </div>
            {segments.length === 0 ? (
              <p className="text-xs text-gray-400">No segments yet.</p>
            ) : (
              <div className="space-y-1">
                {segments.map(seg => (
                  <div key={seg.FileName} className="flex items-center justify-between text-xs bg-gray-50 dark:bg-gray-700 rounded px-2 py-1.5 gap-2">
                    <span className="truncate flex-1" title={seg.FileName}>{seg.FileName}</span>
                    <span className="text-gray-500 dark:text-gray-400 whitespace-nowrap">{(seg.SizeBytes / (1024 * 1024)).toFixed(1)} MB</span>
                    <span className="text-gray-400 whitespace-nowrap hidden sm:inline">{new Date(seg.LastWriteTimeUtc).toLocaleString()}</span>
                    <a href={api.getRecordSinkDownloadUrl(s.Sink.Id, seg.FileName)} className="text-blue-600 hover:text-blue-700" title="Download">
                      <Download className="w-4 h-4" />
                    </a>
                    <button onClick={() => promoteSegment(s.Sink.Id, seg.FileName)} disabled={promoting === `${s.Sink.Id}:${seg.FileName}`}
                      className="text-blue-600 hover:text-blue-700 disabled:opacity-50" title="Use as a Video File source">
                      <FolderInput className="w-4 h-4" />
                    </button>
                    <button onClick={() => deleteSegment(s.Sink.Id, seg.FileName)} className="text-red-600 hover:text-red-700" title="Delete">
                      <Trash2 className="w-4 h-4" />
                    </button>
                  </div>
                ))}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
};
