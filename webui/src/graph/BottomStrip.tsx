import React from 'react';
import { Cpu, Thermometer, Gauge, Radio, Video } from 'lucide-react';
import type { StateSnapshot } from '../types';
import { StreamView } from '../components/StreamView';

// Preview thumbnails for running WebRTC sinks plus an aggregate device/NT4 status bar, derived
// from the /ws/state snapshot. NT4 shows the count of enabled sinks, not per-sink connection state.
export const BottomStrip: React.FC<{ snapshot: StateSnapshot | null; excludeSinkId?: number | null }> = ({ snapshot, excludeSinkId }) => {
  if (!snapshot) return null;

  // skipped: Inspector already shows this sink, and a WebRTC sink holds only one connection
  const webrtcSinks = snapshot.Sinks.filter(s => s.Sink.Type === 5 && s.IsRunning && s.Sink.Id !== excludeSinkId);
  const nt4SinksRunning = snapshot.Sinks.filter(s => s.Sink.Type === 4 && s.IsRunning).length;
  const aggregateFps = Object.values(snapshot.NodeStats).reduce((sum, n) => sum + n.Fps, 0);

  return (
    // normal flow (not an overlay), so it never covers the canvas's Controls/MiniMap
    <div className="flex-shrink-0 bg-white/95 dark:bg-gray-800/95 border-t border-gray-200 dark:border-gray-700 backdrop-blur-sm">
      <div className="flex items-center gap-4 px-3 py-1.5 text-xs text-gray-600 dark:text-gray-300 border-b border-gray-100 dark:border-gray-700">
        <span className="flex items-center gap-1" title="CPU usage"><Cpu className="w-3.5 h-3.5" />{snapshot.Device.CpuUsagePercent.toFixed(0)}%</span>
        <span className="flex items-center gap-1" title="CPU temperature"><Thermometer className="w-3.5 h-3.5" />{snapshot.Device.TemperatureC.toFixed(0)}&deg;C</span>
        <span className="flex items-center gap-1" title="Sum of every node's own FPS"><Gauge className="w-3.5 h-3.5" />{aggregateFps.toFixed(0)} fps aggregate</span>
        <span className={`flex items-center gap-1 ${nt4SinksRunning > 0 ? 'text-blue-600 dark:text-blue-400' : ''}`} title="NetworkTablesSink nodes currently toggled on">
          <Radio className="w-3.5 h-3.5" />{nt4SinksRunning > 0 ? `NT4 publishing (${nt4SinksRunning})` : 'NT4 idle'}
        </span>
      </div>

      {webrtcSinks.length > 0 && (
        <div className="flex items-center gap-2 px-3 py-2 overflow-x-auto">
          {webrtcSinks.map(({ Sink: s }) => (
            <div key={s.Id} className="flex-shrink-0 w-40 h-24 rounded overflow-hidden border border-gray-200 dark:border-gray-700 relative">
              <StreamView sinkId={s.Id} sourceId={s.Source?.Id ?? null} onStop={() => {}} onError={() => {}} compact className="w-full h-full" />
              <span className="absolute bottom-0 left-0 right-0 bg-black/60 text-white text-[10px] px-1 py-0.5 truncate flex items-center gap-1">
                <Video className="w-2.5 h-2.5" />{s.Name}
              </span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
};
