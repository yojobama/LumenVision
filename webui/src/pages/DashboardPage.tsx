import React from 'react';
import {
  Camera,
  Target,
  BarChart3,
  CheckCircle,
  Circle,
  XCircle,
  MonitorSpeaker,
  Activity,
  StopCircle,
  PlayCircle,
  ExternalLink,
} from 'lucide-react';
import type { Source, Sink, SystemStats, DeviceStats } from '../types';
import { StreamView } from '../components/StreamView';
import { AcceleratorCards } from '../components/AcceleratorCards';
import { ToggleSwitch } from '../components/ToggleSwitch';

const SystemStatus: React.FC<{ systemStats: SystemStats; deviceStats: DeviceStats }> = ({ systemStats, deviceStats }) => (
  <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6 mb-6">
    <div className="bg-white dark:bg-gray-800 p-6 rounded-lg shadow transition-all hover:shadow-lg">
      <div className="flex items-center justify-between">
        <div>
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">Sources</h3>
          <p className="text-3xl font-bold text-blue-600 dark:text-blue-400">{systemStats.sources}</p>
        </div>
        <Camera className="w-10 h-10 text-blue-600" />
      </div>
    </div>
    <div className="bg-white dark:bg-gray-800 p-6 rounded-lg shadow transition-all hover:shadow-lg">
      <div className="flex items-center justify-between">
        <div>
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">Sinks</h3>
          <p className="text-3xl font-bold text-green-600 dark:text-green-400">{systemStats.sinks}</p>
        </div>
        <Target className="w-10 h-10 text-green-600" />
      </div>
    </div>
    <div className="bg-white dark:bg-gray-800 p-6 rounded-lg shadow transition-all hover:shadow-lg">
      <div className="flex items-center justify-between">
        <div>
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">CPU Usage</h3>
          <p className="text-3xl font-bold text-purple-600 dark:text-purple-400">{deviceStats.cpuUsage}%</p>
        </div>
        <Activity className="w-10 h-10 text-purple-600" />
      </div>
    </div>
    <div className="bg-white dark:bg-gray-800 p-6 rounded-lg shadow transition-all hover:shadow-lg">
      <div className="flex items-center justify-between">
        <div>
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">Server Status</h3>
          <p className={`text-3xl font-bold ${
            systemStats.serverStatus === 'online' ? 'text-green-600 dark:text-green-400' :
            systemStats.serverStatus === 'offline' ? 'text-gray-600 dark:text-gray-400' :
            'text-red-600 dark:text-red-400'
          }`}>
            {systemStats.serverStatus === 'online' ? 'Online' :
             systemStats.serverStatus === 'offline' ? 'Offline' : 'Error'}
          </p>
        </div>
        {systemStats.serverStatus === 'online' ?
          <CheckCircle className="w-10 h-10 text-green-600" /> :
         systemStats.serverStatus === 'offline' ?
          <Circle className="w-10 h-10 text-gray-600" /> :
          <XCircle className="w-10 h-10 text-red-600" />}
      </div>
    </div>
  </div>
);

export const DashboardPage: React.FC<{
  systemStats: SystemStats;
  deviceStats: DeviceStats;
  streamingSinks: Set<number>;
  sources: Source[];
  sinks: Sink[];
  onStopAllStreams: () => void;
  onStopStream: (id: number) => void;
  onStreamError: (id: number, error: string) => void;
  onTogglePreview: (node: { id: number; name: string }) => void;
  onGoToSinks: () => void;
  onGoToSources: () => void;
  onToggleSink: (id: number, enabled: boolean) => void;
}> = ({ systemStats, deviceStats, streamingSinks, sources, sinks, onStopAllStreams, onStopStream, onStreamError, onTogglePreview, onGoToSinks, onGoToSources, onToggleSink }) => {
  // WebRTC/NetworkTables sinks are auto-created by the preview/publish toggles, so they are omitted
  const visibleSinks = sinks.filter(s => s.type !== 'webrtc' && s.type !== 'networktables');
  return (
  <div className="space-y-6">
    <SystemStatus systemStats={systemStats} deviceStats={deviceStats} />

    <AcceleratorCards accelerators={deviceStats.accelerators} />

    {/* Device Stats Row */}
    <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
      <div className="bg-white dark:bg-gray-800 p-6 rounded-lg shadow">
        <div className="flex items-center justify-between">
          <div>
            <h3 className="text-lg font-semibold text-gray-900 dark:text-white">RAM Usage</h3>
            <p className="text-2xl font-bold text-orange-600 dark:text-orange-400">{deviceStats.ramUsage.toFixed(0)} MB</p>
          </div>
          <BarChart3 className="w-8 h-8 text-orange-600" />
        </div>
      </div>
      <div className="bg-white dark:bg-gray-800 p-6 rounded-lg shadow">
        <div className="flex items-center justify-between">
          <div>
            <h3 className="text-lg font-semibold text-gray-900 dark:text-white">Disk Usage</h3>
            <p className="text-2xl font-bold text-red-600 dark:text-red-400">{deviceStats.diskUsage}%</p>
          </div>
          <BarChart3 className="w-8 h-8 text-red-600" />
        </div>
      </div>
    </div>

    <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
      {/* Sources Widget */}
      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Camera className="w-5 h-5 text-blue-600" />Recent Sources</h2>
          <button onClick={onGoToSources} className="text-blue-600 hover:text-blue-700 text-sm font-medium flex items-center gap-1">View All <ExternalLink className="w-3 h-3" /></button>
        </div>
        {sources.length === 0 ? (
          <div className="text-center py-8">
            <Camera className="w-12 h-12 mx-auto mb-3 text-gray-400" />
            <p className="text-gray-500 dark:text-gray-400">No sources available</p>
            <button onClick={onGoToSources} className="mt-2 text-blue-600 hover:text-blue-700 text-sm">Add your first source</button>
          </div>
        ) : (
          <div className="space-y-3 max-h-64 overflow-y-auto">
            {sources.slice(0,4).map(source => (
              <div key={source.id} className="flex items-center justify-between p-3 bg-gray-50 dark:bg-gray-700 rounded-lg">
                <div className="flex items-center gap-3">
                  <div className={`w-3 h-3 rounded-full ${source.status==='active' ? 'bg-green-500' : source.status==='inactive' ? 'bg-gray-400' : 'bg-red-500'}`} />
                  <div>
                    <p className="font-medium text-gray-900 dark:text-white">{source.name}</p>
                    <p className="text-xs text-gray-500 dark:text-gray-400">{source.type}</p>
                  </div>
                </div>
                <div className="text-xs text-gray-400">ID: {source.id}</div>
              </div>
            ))}
            {sources.length > 4 && (
              <div className="text-center pt-2">
                <button onClick={onGoToSources} className="text-blue-600 hover:text-blue-700 text-sm">+{sources.length - 4} more sources</button>
              </div>
            )}
          </div>
        )}
      </div>
      {/* Sinks Widget */}
      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Target className="w-5 h-5 text-green-600" />Processing Sinks</h2>
          <button onClick={onGoToSinks} className="text-blue-600 hover:text-blue-700 text-sm font-medium flex items-center gap-1">View All <ExternalLink className="w-3 h-3" /></button>
        </div>
        {visibleSinks.length === 0 ? (
          <div className="text-center py-8">
            <Target className="w-12 h-12 mx-auto mb-3 text-gray-400" />
            <p className="text-gray-500 dark:text-gray-400">No sinks available</p>
            <button onClick={onGoToSinks} className="mt-2 text-blue-600 hover:text-blue-700 text-sm">Add your first sink</button>
          </div>
        ) : (
          <div className="space-y-3 max-h-64 overflow-y-auto">
            {visibleSinks.slice(0,4).map(sink => {
              const preview = sinks.find(s => s.type === 'webrtc' && s.sourceId === sink.id);
              const isStreaming = preview != null && streamingSinks.has(preview.id);
              return (
              <div key={sink.id} className="flex items-center justify-between p-3 bg-gray-50 dark:bg-gray-700 rounded-lg">
                <div className="flex items-center gap-3">
                  <div className={`w-3 h-3 rounded-full ${sink.status==='active' ? 'bg-green-500' : sink.status==='inactive' ? 'bg-gray-400' : 'bg-red-500'}`} />
                  <div>
                    <p className="font-medium text-gray-900 dark:text-white">{sink.name}</p>
                    <p className="text-xs text-gray-500 dark:text-gray-400">{sink.type}</p>
                  </div>
                </div>
                <div className="flex items-center gap-2">
                  <ToggleSwitch
                    enabled={sink.isEnabled ?? false}
                    onChange={(enabled) => onToggleSink(sink.id, enabled)}
                  />
                  {isStreaming ? (
                    <>
                      <span className="px-2 py-1 bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200 rounded text-xs font-medium">LIVE</span>
                      <button onClick={()=>onTogglePreview({id: sink.id, name: sink.name})} className="p-1 text-red-600 hover:text-red-700" title="Stop preview"><StopCircle className="w-4 h-4" /></button>
                    </>
                  ) : (
                    <button onClick={()=>onTogglePreview({id: sink.id, name: sink.name})} className="p-1 text-green-600 hover:text-green-700" title="Start preview" disabled={sink.status==='error'}><PlayCircle className="w-4 h-4" /></button>
                  )}
                </div>
              </div>
              );
            })}
            {visibleSinks.length > 4 && (
              <div className="text-center pt-2">
                <button onClick={onGoToSinks} className="text-blue-600 hover:text-blue-700 text-sm">+{visibleSinks.length - 4} more sinks</button>
              </div>
            )}
          </div>
        )}
      </div>
    </div>

    {streamingSinks.size > 0 && (
      <div className="space-y-4">
        <div className="flex items-center justify-between">
          <h2 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Activity className="w-5 h-5" />Live Streams</h2>
          <button onClick={onStopAllStreams} className="px-3 py-1 bg-red-600 text-white rounded text-sm hover:bg-red-700 flex items-center gap-1" title="Stop all streams"><StopCircle className="w-4 h-4" />Stop All Streams</button>
        </div>
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          {Array.from(streamingSinks).map(sinkId => (
            <StreamView
              key={sinkId}
              sinkId={sinkId}
              sourceId={sinks.find(s => s.id === sinkId)?.sourceId ?? null}
              onStop={() => onStopStream(sinkId)}
              onError={(error) => onStreamError(sinkId, error)}
            />
          ))}
        </div>
      </div>
    )}
    {streamingSinks.size === 0 && (
      <div className="text-center py-12">
        <MonitorSpeaker className="w-16 h-16 mx-auto mb-4 text-gray-400" />
        <h3 className="text-xl font-semibold text-gray-900 dark:text-white mb-2">No Active Streams</h3>
        <p className="text-gray-600 dark:text-gray-400 mb-4">Activate Live Preview on a node from the Graph editor to see it here.</p>
        <button onClick={onGoToSinks} className="px-4 py-2 bg-blue-600 text-white rounded hover:bg-blue-700 flex items-center gap-2 mx-auto"><Target className="w-4 h-4" />Go to Graph</button>
      </div>
    )}
  </div>
  );
};
