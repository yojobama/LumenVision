import React, { useEffect, useState } from 'react';
import { Gauge, Wifi, WifiOff, Thermometer, Cpu, HardDrive, MemoryStick, Circle, Square } from 'lucide-react';
import { useStateSocket } from '../hooks/useStateSocket';
import { ApiService } from '../services/ApiService';
import { sinkTypeName } from '../graph/model';
import type { NetworkTablesStatus } from '../types';

const api = new ApiService();

// Read-only per-camera FPS/latency/NT4/temperature view. FPS/latency/device stats come from
// /ws/state; NT4 connection state is fetched over REST.
export const MatchPage: React.FC = () => {
  const { snapshot, connected } = useStateSocket();
  const [nt4Status, setNt4Status] = useState<Record<number, NetworkTablesStatus>>({});
  const [togglingRecording, setTogglingRecording] = useState(false);

  useEffect(() => {
    if (!snapshot) return;
    const nt4Sinks = snapshot.Sinks.filter(s => sinkTypeName(s.Sink.Type) === 'NetworkTablesSink');
    let cancelled = false;
    Promise.all(nt4Sinks.map(async s => {
      try {
        const status = await api.getNetworkTablesStatus(s.Sink.Id);
        return [s.Sink.Id, status] as const;
      } catch {
        return null;
      }
    })).then(results => {
      if (cancelled) return;
      const next: Record<number, NetworkTablesStatus> = {};
      for (const r of results) if (r) next[r[0]] = r[1];
      setNt4Status(next);
    });
    return () => { cancelled = true; };
    // re-fetch when the sink topology changes; the interval covers connection-state changes
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [snapshot?.Sinks.length]);

  if (!snapshot) {
    return (
      <div className="flex flex-col items-center justify-center py-24 text-center">
        <Gauge className="w-16 h-16 mb-4 text-gray-400" />
        <h2 className="text-xl font-semibold text-gray-900 dark:text-white mb-2">Match View</h2>
        <p className="text-gray-600 dark:text-gray-400">{connected ? 'Waiting for the first state snapshot…' : 'Connecting to the coprocessor…'}</p>
      </div>
    );
  }

  // per camera: find the bound detection sink and the NetworkTablesSink bound to its output;
  // the recordSink is bound directly to the raw source
  const rows = snapshot.Sources.map(source => {
    const detector = snapshot.Sinks.find(s => s.Sink.Source?.Id === source.Id);
    const nt4Sink = detector
      ? snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'NetworkTablesSink' && s.Sink.Source?.Id === detector.Sink.Id)
      : undefined;
    const recordSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'RecordSink' && s.Sink.Source?.Id === source.Id);
    const stats = snapshot.NodeStats[String(source.Id)];
    const status = nt4Sink ? nt4Status[nt4Sink.Sink.Id] : undefined;
    return { source, detector, nt4Sink, recordSink, stats, status };
  });

  // one button for every camera; logic lives in SinkManager.SetAllRecording, the same code robot
  // code triggers over NT (<root>/config/recording)
  const anyRecording = rows.some(r => r.recordSink?.IsRunning);
  const toggleAllRecording = async () => {
    setTogglingRecording(true);
    try {
      await api.setAllRecording(!anyRecording);
    } finally {
      setTogglingRecording(false);
    }
  };

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Gauge className="w-6 h-6" />Match View</h2>
        <div className="flex items-center gap-3">
          <span className={`px-3 py-1 rounded-full text-sm font-medium flex items-center gap-1 ${connected ? 'bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-200' : 'bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200'}`}>
            {connected ? <Wifi className="w-4 h-4" /> : <WifiOff className="w-4 h-4" />}{connected ? 'Live' : 'Disconnected'}
          </span>
          <button onClick={toggleAllRecording} disabled={togglingRecording || rows.length === 0}
            className={`px-4 py-1.5 rounded-full text-sm font-medium flex items-center gap-2 text-white disabled:opacity-50 ${anyRecording ? 'bg-red-600 hover:bg-red-700' : 'bg-gray-600 hover:bg-gray-700'}`}>
            {anyRecording ? <Square className="w-4 h-4" /> : <Circle className="w-4 h-4" />}
            {anyRecording ? 'Stop Recording' : 'Start Recording'}
          </button>
        </div>
      </div>

      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 flex items-center gap-3">
          <Cpu className="w-6 h-6 text-purple-600" />
          <div><div className="text-xs text-gray-500 dark:text-gray-400">CPU</div><div className="text-lg font-bold text-gray-900 dark:text-white">{snapshot.Device.CpuUsagePercent}%</div></div>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 flex items-center gap-3">
          <MemoryStick className="w-6 h-6 text-orange-600" />
          <div><div className="text-xs text-gray-500 dark:text-gray-400">RAM</div><div className="text-lg font-bold text-gray-900 dark:text-white">{snapshot.Device.RamUsageMb} MB</div></div>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 flex items-center gap-3">
          <HardDrive className="w-6 h-6 text-red-600" />
          <div><div className="text-xs text-gray-500 dark:text-gray-400">Disk</div><div className="text-lg font-bold text-gray-900 dark:text-white">{snapshot.Device.DiskUsagePercent}%</div></div>
        </div>
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 flex items-center gap-3">
          <Thermometer className="w-6 h-6 text-blue-600" />
          <div><div className="text-xs text-gray-500 dark:text-gray-400">Temperature</div><div className="text-lg font-bold text-gray-900 dark:text-white">{snapshot.Device.TemperatureC > 0 ? `${snapshot.Device.TemperatureC}°C` : '—'}</div></div>
        </div>
      </div>

      <div className="bg-white dark:bg-gray-800 rounded-lg shadow overflow-hidden">
        <table className="w-full text-sm">
          <thead className="bg-gray-50 dark:bg-gray-700 text-gray-600 dark:text-gray-300">
            <tr>
              <th className="text-left px-4 py-2">Camera</th>
              <th className="text-left px-4 py-2">Detector</th>
              <th className="text-left px-4 py-2">FPS</th>
              <th className="text-left px-4 py-2">Latency</th>
              <th className="text-left px-4 py-2">NT4</th>
              <th className="text-left px-4 py-2">Recording</th>
            </tr>
          </thead>
          <tbody>
            {rows.map(({ source, detector, recordSink, stats, status }) => (
              <tr key={source.Id} className="border-t border-gray-100 dark:border-gray-700">
                <td className="px-4 py-3 text-gray-900 dark:text-white font-medium">{source.Name} <span className="text-gray-400 text-xs">#{source.Id}</span></td>
                <td className="px-4 py-3 text-gray-600 dark:text-gray-400">{detector ? `${sinkTypeName(detector.Sink.Type)} (#${detector.Sink.Id})` : '—'}</td>
                <td className="px-4 py-3 text-gray-900 dark:text-white">{(stats?.Fps ?? 0).toFixed(1)}</td>
                <td className="px-4 py-3 text-gray-900 dark:text-white">{((stats?.LatencyUs ?? 0) / 1000).toFixed(1)} ms</td>
                <td className="px-4 py-3">
                  {status ? (
                    <span className={`inline-flex items-center gap-1 px-2 py-0.5 rounded text-xs font-medium ${status.Connected ? 'bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-200' : 'bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200'}`}>
                      {status.Connected ? <Wifi className="w-3 h-3" /> : <WifiOff className="w-3 h-3" />}{status.Connected ? 'Connected' : 'Disconnected'}
                    </span>
                  ) : <span className="text-gray-400 text-xs">not publishing</span>}
                </td>
                <td className="px-4 py-3">
                  {recordSink?.IsRunning ? (
                    <span className="inline-flex items-center gap-1 px-2 py-0.5 rounded text-xs font-medium bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200">
                      <Circle className="w-2.5 h-2.5 fill-current" />Recording
                    </span>
                  ) : <span className="text-gray-400 text-xs">stopped</span>}
                </td>
              </tr>
            ))}
            {rows.length === 0 && (
              <tr><td colSpan={6} className="px-4 py-8 text-center text-gray-400">No camera sources yet</td></tr>
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
};
