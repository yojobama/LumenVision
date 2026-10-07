import { useEffect, useMemo, useState } from 'react';
import type { Source, Sink, SystemStats, Toast, DeviceStats, NT4Defaults } from '../types';
import { ApiService } from '../services/ApiService';
import { useStateSocket } from './useStateSocket';

// Maps the SinkType enum ordinal to this UI's label; mirrors Server/Sink.cs (2 is reserved):
// ApriltagSink=0, ObjectDetectionSink=1, CameraCalibrationSink=3, NetworkTablesSink=4, WebRTCSink=5,
// StereoCalibrationSink=6, StereoDepthSink=7, DepthFusionSink=8, MjpegSink=9, RecordSink=10.
export const mapSinkType = (type: any): string => {
  if (typeof type === 'string') return type;
  switch (type) {
    case 0: return 'apriltag';
    case 1: return 'object';
    case 3: return 'calibration';
    case 4: return 'networktables';
    case 5: return 'webrtc';
    case 6: return 'stereocalibration';
    case 7: return 'stereodepth';
    case 8: return 'depthfusion';
    case 9: return 'mjpeg';
    case 10: return 'record';
    default: return 'unknown';
  }
};

// Same for Server/Source.cs's SourceType: Camera=0, ImageFile=1, VideoFile=2, SinkOutput=3.
const mapSourceType = (type: any): string => {
  if (typeof type === 'string') return type;
  switch (type) {
    case 0: return 'camera';
    case 1: return 'image';
    case 2: return 'video';
    case 3: return 'sinkoutput';
    default: return 'unknown';
  }
};

// Data comes from the /ws/state push channel (useStateSocket); the REST calls here are actions.
export const useAppData = () => {
  const { snapshot, connected } = useStateSocket();
  const [streamingSinks, setStreamingSinks] = useState<Set<number>>(new Set());
  const [error, setError] = useState<string | null>(null);
  const [toast, setToast] = useState<Toast | null>(null);

  const api = new ApiService();

  // derived from the latest snapshot, not stored separately
  const sources: Source[] = useMemo(() => {
    if (!snapshot) return [];
    return snapshot.Sources.map(s => ({
      id: s.Id,
      name: s.Name || `Source ${s.Id}`,
      type: mapSourceType(s.Type),
      status: 'active' as const,
      lastUpdate: new Date(),
      filePath: s.FilePath ?? undefined,
      fps: s.Fps ?? undefined,
      cameraHardwareInfo: s.CameraHardwareInfo
        ? { Name: s.CameraHardwareInfo.name, Path: s.CameraHardwareInfo.path }
        : undefined,
    }));
  }, [snapshot]);

  const sinks: Sink[] = useMemo(() => {
    if (!snapshot) return [];
    return snapshot.Sinks.map(({ Sink: s, IsRunning }) => ({
      id: s.Id,
      name: s.Name || `Sink ${s.Id}`,
      type: mapSinkType(s.Type),
      status: 'active' as const,
      lastUpdate: new Date(),
      sourceId: s.Source?.Id,
      source2Id: s.Source2?.Id,
      isEnabled: IsRunning,
    }));
  }, [snapshot]);

  // MB: WsDeviceStats already reports RamUsageMb
  const deviceStats: DeviceStats = useMemo(() => ({
    cpuUsage: snapshot?.Device.CpuUsagePercent ?? 0,
    ramUsage: snapshot?.Device.RamUsageMb ?? 0,
    diskUsage: snapshot?.Device.DiskUsagePercent ?? 0,
    accelerators: snapshot?.Device.Accelerators ?? [],
  }), [snapshot]);

  const systemStats: SystemStats = useMemo(() => ({
    sources: sources.length,
    sinks: sinks.length,
    activeStreams: streamingSinks.size,
    uptime: new Date().toLocaleTimeString(),
    serverStatus: snapshot ? 'online' : (connected ? 'online' : 'offline'),
  }), [sources.length, sinks.length, streamingSinks.size, snapshot, connected]);

  // true only until the first snapshot; the last snapshot is kept across reconnects
  const loading = snapshot === null;

  const stopStream = (sinkId: number) => {
    setStreamingSinks(prev => {
      const newSet = new Set(prev);
      newSet.delete(sinkId);
      return newSet;
    });
    showToast(`Stopped streaming for Sink ${sinkId}`, 'info');
  };

  const handleStreamError = (sinkId: number, error: string) => {
    stopStream(sinkId);
    showToast(`Stream error for Sink ${sinkId}: ${error}`, 'error');
  };

  const showToast = (message: string, type: 'success' | 'error' | 'info') => {
    setToast({ message, type });
  };

  // "Live Preview" toggle for a Source or detector Sink; creates a dedicated WebRTCSink bound to it on first use.
  const handleTogglePreview = async (node: { id: number; name: string }) => {
    try {
      const companion = sinks.find(s => s.type === 'webrtc' && s.sourceId === node.id);
      if (companion) {
        if (streamingSinks.has(companion.id)) {
          stopStream(companion.id);
          return;
        }
        if (!companion.isEnabled) {
          await api.toggleSink(companion.id, true);
        }
        setStreamingSinks(prev => new Set(prev).add(companion.id));
        return;
      }

      const sinkId = await api.createWebRTCSink(`${node.name}-preview`);
      await api.bindSinkToSource(sinkId, node.id);
      await api.toggleSink(sinkId, true);
      setStreamingSinks(prev => new Set(prev).add(sinkId));
    } catch (error) {
      showToast(`Failed to start preview: ${error}`, 'error');
    }
  };

  // "Publish to NetworkTables" toggle for a detector sink; creates a dedicated NetworkTablesSink on first use,
  // reusing the connection details from Settings.
  const handleToggleNT4Publish = async (node: { id: number; name: string }, nt4: NT4Defaults) => {
    try {
      const companion = sinks.find(s => s.type === 'networktables' && s.sourceId === node.id);
      if (companion) {
        await handleToggleSink(companion.id, !companion.isEnabled);
        return;
      }

      if (nt4.mode === 'team' && !nt4.teamNumber) {
        showToast('Set a NetworkTables team number in Settings first', 'error');
        return;
      }
      if (nt4.mode === 'server' && !nt4.serverAddress) {
        showToast('Set a NetworkTables server address in Settings first', 'error');
        return;
      }

      const sinkId = nt4.mode === 'server'
        ? await api.createNetworkTablesSinkForServer(`${node.name}-nt4`, nt4.serverAddress!, nt4.port ?? 0, nt4.rootTable)
        : await api.createNetworkTablesSinkForTeam(`${node.name}-nt4`, nt4.teamNumber!, nt4.rootTable);
      await api.bindSinkToSource(sinkId, node.id);
      await api.toggleSink(sinkId, true);
      showToast(`Publishing "${node.name}" to NetworkTables`, 'success');
    } catch (error) {
      showToast(`Failed to toggle NetworkTables publishing: ${error}`, 'error');
    }
  };

  const handleToggleSink = async (sinkId: number, enabled: boolean) => {
    try {
      await api.toggleSink(sinkId, enabled);
      showToast(`Sink ${enabled ? 'enabled' : 'disabled'}`, 'success');
      // no optimistic update needed: the next /ws/state tick reflects the result
    } catch (error) {
      showToast(`Failed to toggle sink: ${error}`, 'error');
    }
  };

  useEffect(() => {
    if (!connected) setError('Reconnecting to the server...');
    else setError(null);
  }, [connected]);

  // no-op: data is live-derived from the socket snapshot
  const loadData = () => {};

  return {
    sources,
    sinks,
    streamingSinks,
    loading,
    error,
    systemStats,
    deviceStats,
    toast,
    loadData,
    stopStream,
    handleStreamError,
    showToast,
    handleToggleSink,
    handleTogglePreview,
    handleToggleNT4Publish,
    setStreamingSinks,
    setError,
    setToast
  };
};
