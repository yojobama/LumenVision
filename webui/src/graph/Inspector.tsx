import React, { useEffect, useState } from 'react';
import { X, Trash2, Wifi, WifiOff, Radio, Play, Square, Code, RefreshCw, AlertTriangle, Circle, Download, FolderInput } from 'lucide-react';
import type { PipelineNode } from './model';
import type { WsSource, WsSink, NT4Defaults, CameraMode, CameraControls, CalibrationStatus, RecordSegment } from '../types';
import { REFINE_EDGES_MODES } from '../types';
import { ApiService } from '../services/ApiService';
import { ToggleSwitch } from '../components/ToggleSwitch';
import { StreamView } from '../components/StreamView';

const api = new ApiService();

// LumenCore/FrameFormat.h's declaration order - see CameraMode's own comment in types/index.ts.
const PIXEL_FORMAT_NAMES = ['BGR24', 'RGB24', 'GRAY8', 'NV12', 'YUYV', 'MJPEG', 'Y10', 'Y16', 'Y10P', 'Y10BPACK'];
const modeLabel = (m: CameraMode) => `${m.Width}x${m.Height} @ ${m.Fps}fps (${PIXEL_FORMAT_NAMES[m.PixelFormat] ?? m.PixelFormat})`;
const modeKey = (m: CameraMode) => `${m.Width}x${m.Height}x${m.Fps}x${m.PixelFormat}`;

// Right-hand inspector for the selected node: live parameters, preview, latest result JSON and
// controls. Node creation and connection happen on the canvas.
export const Inspector: React.FC<{
  node: PipelineNode;
  onClose: () => void;
  onToast: (message: string, type: 'success' | 'error' | 'info') => void;
  onDeleted: () => void;
  nt4Settings: NT4Defaults;
}> = ({ node, onClose, onToast, onDeleted, nt4Settings }) => {
  const { kind, raw, webrtcSink, nt4Sink, recordSink, isRunning } = node.data;
  const [name, setName] = useState(node.data.label);
  const [resultJson, setResultJson] = useState<string | null>(null);
  const [showPreview, setShowPreview] = useState(false);
  const [segments, setSegments] = useState<RecordSegment[]>([]);
  const [promotingSegment, setPromotingSegment] = useState<string | null>(null);
  const [newProfileName, setNewProfileName] = useState('');
  const [newProfileTagSize, setNewProfileTagSize] = useState(0.1651);
  // 0 = CPU (apriltag), 1 = Vulkan (vkapriltag)
  const [newProfileBackend, setNewProfileBackend] = useState(0);
  const [cameraModes, setCameraModes] = useState<CameraMode[]>([]);
  const [currentMode, setCurrentMode] = useState<CameraMode | null>(null);
  const [calibrationStatus, setCalibrationStatus] = useState<CalibrationStatus | null>(null);
  const [autoExposure, setAutoExposure] = useState(true);
  const [exposureValue, setExposureValue] = useState(300);
  const [gainValue, setGainValue] = useState(0);
  const [cameraControls, setCameraControls] = useState<CameraControls | null>(null);
  const [sinkBackend, setSinkBackend] = useState<number | null>(null);
  const [switchingBackend, setSwitchingBackend] = useState(false);
  // threads/quadDecimate/refineEdges are user-adjustable; Vulkan decimation is an integer that must
  // divide the frame size, so the server reports the value it actually runs.
  const [threadsValue, setThreadsValue] = useState(0);
  const [quadDecimateValue, setQuadDecimateValue] = useState(0);
  const [quadDecimateSupported, setQuadDecimateSupported] = useState(true);
  const [refineEdgesValue, setRefineEdgesValue] = useState(true);
  const [refineModeValue, setRefineModeValue] = useState(1);
  const [refineModeSupported, setRefineModeSupported] = useState(false);
  const [applyingTuning, setApplyingTuning] = useState(false);

  useEffect(() => {
    setName(node.data.label);
    setResultJson(null);
    setShowPreview(false);
    setCameraModes([]);
    setCurrentMode(null);
    setCalibrationStatus(null);
    setCameraControls(null);
  }, [node.id]);

  const source = kind === 'source' ? (raw as WsSource) : null;
  const sink = kind === 'sink' ? (raw as WsSink) : null;
  const isCamera = source != null && source.Type === 0;
  // Live Preview binds a WebRTCSink to the selected node, whether a Source or a Sink output.
  const previewTarget = sink ?? source;

  // Modes, controls and calibration status are not in the /ws/state snapshot, so fetch them once
  // per selected camera source.
  useEffect(() => {
    if (!isCamera || !source) return;
    let cancelled = false;
    Promise.all([api.getCameraModes(source.Id), api.getCameraCurrentMode(source.Id), api.getCalibrationStatus(source.Id)])
      .then(([modes, mode, calibration]) => {
        if (cancelled) return;
        setCameraModes(modes);
        setCurrentMode(mode);
        setCalibrationStatus(calibration);
      })
      .catch(() => { if (!cancelled) onToast('Failed to load camera modes', 'error'); });
    // separate from the batch above so a server without /controls still shows the mode picker
    api.getCameraControls(source.Id)
      .then(controls => {
        if (cancelled) return;
        setCameraControls(controls);
        if (controls.Exposure.Supported) setExposureValue(controls.Exposure.Value);
        if (controls.Gain.Supported) setGainValue(controls.Gain.Value);
      })
      .catch(() => {});
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [node.id, isCamera]);

  const changeMode = async (mode: CameraMode) => {
    if (!source) return;
    try {
      await api.setCameraMode(source.Id, mode);
      const applied = await api.getCameraCurrentMode(source.Id);
      setCurrentMode(applied);
      onToast(applied.IsNative ? 'Mode applied' : 'Camera substituted the nearest supported mode', applied.IsNative ? 'success' : 'info');
      // re-check calibration staleness immediately
      api.getCalibrationStatus(source.Id).then(setCalibrationStatus).catch(() => {});
    } catch {
      onToast('Failed to set camera mode', 'error');
    }
  };

  const changeAutoExposure = async (enabled: boolean) => {
    if (!source) return;
    try {
      await api.setCameraAutoExposure(source.Id, enabled);
      setAutoExposure(enabled);
    } catch {
      onToast('Auto-exposure not supported by this device', 'error');
    }
  };

  const applyExposure = async () => {
    if (!source) return;
    try {
      // false = the ioctl was rejected (e.g. out of range for this camera), not a network error
      const ok = await api.setCameraExposure(source.Id, exposureValue);
      onToast(ok ? 'Exposure applied' : 'Camera rejected that exposure value', ok ? 'success' : 'error');
    } catch {
      onToast('Exposure not supported by this device', 'error');
    }
  };

  const applyGain = async () => {
    if (!source) return;
    try {
      const ok = await api.setCameraGain(source.Id, gainValue);
      onToast(ok ? 'Gain applied' : 'Camera rejected that gain value', ok ? 'success' : 'error');
    } catch {
      onToast('Gain not supported by this device', 'error');
    }
  };

  const isApriltagSink = sink != null && node.data.typeName === 'ApriltagSink';
  const isObjectDetectionSink = sink != null && node.data.typeName === 'ObjectDetectionSink';
  const [detectionBackendName, setDetectionBackendName] = useState<string | null>(null);

  // read-only: no backend switcher for object detection
  useEffect(() => {
    if (!isObjectDetectionSink || !sink) return;
    let cancelled = false;
    api.getObjectDetectionSinkBackend(sink.Id)
      .then(name => { if (!cancelled) setDetectionBackendName(name); })
      .catch(() => { if (!cancelled) setDetectionBackendName(null); });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [node.id, isObjectDetectionSink]);

  // sink backend/tuning is not in the snapshot, so fetch it once per selected node
  useEffect(() => {
    if (!isApriltagSink || !sink) return;
    let cancelled = false;
    api.getApriltagBackendKind(sink.Id)
      .then(backend => { if (!cancelled) setSinkBackend(backend); })
      .catch(() => { if (!cancelled) onToast('Failed to load detector backend', 'error'); });
    api.getApriltagTuning(sink.Id)
      .then(tuning => {
        if (cancelled) return;
        setThreadsValue(tuning.threads);
        setQuadDecimateValue(tuning.quadDecimate);
        setQuadDecimateSupported(tuning.quadDecimateSupported);
        setRefineEdgesValue(tuning.refineEdges);
        setRefineModeValue(tuning.refineMode);
        setRefineModeSupported(tuning.refineModeSupported);
      })
      .catch(() => { if (!cancelled) onToast('Failed to load detector tuning', 'error'); });
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [node.id, isApriltagSink]);

  // Rebuilds the detector in place, preserving id/tag size/calibration/bindings, so an open preview
  // may briefly go black. Carries the current tuning forward.
  const switchBackend = async (backend: number) => {
    if (!sink) return;
    setSwitchingBackend(true);
    try {
      await api.setApriltagBackend(sink.Id, backend, threadsValue, quadDecimateValue, refineEdgesValue, refineModeValue);
      setSinkBackend(backend);
      const actual = await api.getApriltagTuning(sink.Id);
      setRefineModeValue(actual.refineMode);
      setRefineModeSupported(actual.refineModeSupported);
      onToast('Backend switched', 'success');
    } catch {
      onToast('Failed to switch backend', 'error');
    } finally {
      setSwitchingBackend(false);
    }
  };

  // Applies tuning without changing backend, then re-reads it (Vulkan may round decimation).
  const applyTuning = async () => {
    if (!sink || sinkBackend === null) return;
    setApplyingTuning(true);
    try {
      await api.setApriltagBackend(sink.Id, sinkBackend, threadsValue, quadDecimateSupported ? quadDecimateValue : undefined, refineEdgesValue, refineModeValue);
      const actual = await api.getApriltagTuning(sink.Id);
      setThreadsValue(actual.threads);
      setQuadDecimateValue(actual.quadDecimate);
      setRefineEdgesValue(actual.refineEdges);
      setRefineModeValue(actual.refineMode);
      setRefineModeSupported(actual.refineModeSupported);
      onToast('Tuning applied', 'success');
    } catch {
      onToast('Failed to apply tuning', 'error');
    } finally {
      setApplyingTuning(false);
    }
  };

  const saveName = async () => {
    try {
      if (source) await api.changeSourceName(source.Id, name);
      else if (sink) await api.renameSink(sink.Id, name);
      onToast('Renamed', 'success');
    } catch {
      onToast('Rename failed', 'error');
    }
  };

  const handleDelete = async () => {
    if (!confirm(`Delete ${node.data.label}?`)) return;
    try {
      if (source) await api.deleteSource(source.Id);
      else if (sink) await api.deleteSink(sink.Id);
      onToast('Deleted', 'info');
      onDeleted();
    } catch {
      onToast('Delete failed', 'error');
    }
  };

  const toggleEnabled = async (enabled: boolean) => {
    if (!sink) return;
    try {
      await api.toggleSink(sink.Id, enabled);
    } catch {
      onToast('Failed to toggle sink', 'error');
    }
  };

  const togglePreview = async () => {
    if (!previewTarget) return;
    try {
      if (webrtcSink) {
        await api.toggleSink(webrtcSink.Sink.Id, !webrtcSink.IsRunning);
      } else {
        const previewId = await api.createWebRTCSink(`${previewTarget.Name}-preview`);
        await api.bindSinkToSource(previewId, previewTarget.Id);
        await api.toggleSink(previewId, true);
      }
    } catch {
      onToast('Failed to toggle preview', 'error');
    }
  };

  // a RecordSink bound to the selected node, like the preview sink
  const toggleRecording = async () => {
    if (!previewTarget) return;
    try {
      if (recordSink) {
        await api.toggleSink(recordSink.Sink.Id, !recordSink.IsRunning);
      } else {
        const recordId = await api.createRecordSink(`${previewTarget.Name}-recording`);
        await api.bindSinkToSource(recordId, previewTarget.Id);
        await api.toggleSink(recordId, true);
      }
    } catch {
      onToast('Failed to toggle recording', 'error');
    }
  };

  const refreshSegments = async () => {
    if (!recordSink) { setSegments([]); return; }
    try {
      setSegments(await api.getRecordSinkSegments(recordSink.Sink.Id));
    } catch {
      onToast('Failed to load recorded segments', 'error');
    }
  };

  // re-fetch when recording starts/stops (segment finalised on stop) or the node changes
  useEffect(() => {
    refreshSegments();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [recordSink?.Sink.Id, recordSink?.IsRunning]);

  const promoteSegment = async (fileName: string) => {
    if (!recordSink) return;
    setPromotingSegment(fileName);
    try {
      await api.promoteRecordSinkSegment(recordSink.Sink.Id, fileName);
      onToast(`"${fileName}" is now available as a Video File source`, 'success');
    } catch {
      onToast(`Failed to use "${fileName}" as a source`, 'error');
    } finally {
      setPromotingSegment(null);
    }
  };

  const deleteSegment = async (fileName: string) => {
    if (!recordSink) return;
    try {
      await api.deleteRecordSinkSegment(recordSink.Sink.Id, fileName);
      await refreshSegments();
    } catch {
      onToast(`Failed to delete "${fileName}"`, 'error');
    }
  };

  const toggleNT4 = async () => {
    if (!sink) return;
    try {
      if (nt4Sink) {
        await api.toggleSink(nt4Sink.Sink.Id, !nt4Sink.IsRunning);
        return;
      }
      if (nt4Settings.mode === 'team' && !nt4Settings.teamNumber) {
        onToast('Set a NetworkTables team number in Settings first', 'error');
        return;
      }
      const ntId = nt4Settings.mode === 'team'
        ? await api.createNetworkTablesSinkForTeam(`${sink.Name}-nt4`, nt4Settings.teamNumber!, nt4Settings.rootTable)
        : await api.createNetworkTablesSinkForServer(`${sink.Name}-nt4`, nt4Settings.serverAddress!, nt4Settings.port, nt4Settings.rootTable);
      await api.bindSinkToSource(ntId, sink.Id);
      await api.toggleSink(ntId, true);
    } catch {
      onToast('Failed to toggle NT4 publish', 'error');
    }
  };

  const fetchResult = async () => {
    if (!sink) return;
    try {
      const result = await api.getSinkResult(sink.Id);
      setResultJson(JSON.stringify(result, null, 2));
    } catch (err) {
      setResultJson(`(failed to fetch result: ${err instanceof Error ? err.message : String(err)})`);
    }
  };

  const createApriltagProfile = async () => {
    if (!source || !newProfileName.trim()) return;
    try {
      await api.createApriltagProfile(source.Id, newProfileName.trim(), newProfileTagSize, { backend: newProfileBackend });
      setNewProfileName('');
      onToast('Profile created', 'success');
    } catch {
      onToast('Failed to create profile', 'error');
    }
  };

  const activateProfile = async (index: number) => {
    if (!source) return;
    try {
      await api.activateProfile(source.Id, index);
      onToast(`Activated profile ${index}`, 'success');
    } catch {
      onToast('Failed to activate profile', 'error');
    }
  };

  return (
    <div className="w-96 bg-white dark:bg-gray-800 border-l border-gray-200 dark:border-gray-700 flex flex-col h-full overflow-y-auto">
      <div className="p-4 border-b border-gray-200 dark:border-gray-700 flex items-center justify-between">
        <h3 className="font-semibold text-gray-900 dark:text-white">{node.data.typeName}</h3>
        <button onClick={onClose} className="text-gray-500 hover:text-gray-700"><X className="w-4 h-4" /></button>
      </div>

      <div className="p-4 space-y-4">
        <div>
          <label className="block text-xs font-medium text-gray-500 dark:text-gray-400 mb-1">Name</label>
          <div className="flex gap-2">
            <input value={name} onChange={e => setName(e.target.value)} onBlur={saveName}
              className="flex-1 px-2 py-1 text-sm border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
          </div>
        </div>

        <div className="text-xs text-gray-500 dark:text-gray-400">
          ID: {source?.Id ?? sink?.Id} &middot; {node.data.fps.toFixed(1)} fps &middot; {(node.data.latencyUs / 1000).toFixed(1)} ms latency
        </div>

        {isCamera && (
          <div className="pt-2 border-t border-gray-200 dark:border-gray-700 space-y-3">
            <h4 className="text-xs font-medium text-gray-500 dark:text-gray-400">Camera Controls</h4>

            {calibrationStatus?.HasCalibration && !calibrationStatus.MatchesCurrentResolution && (
              <div className="flex items-start gap-1.5 text-xs px-2 py-1.5 rounded bg-amber-100 text-amber-800 dark:bg-amber-900 dark:text-amber-200">
                <AlertTriangle className="w-3.5 h-3.5 mt-0.5 flex-shrink-0" />
                <span>
                  Saved calibration is for {calibrationStatus.CalibratedWidth}x{calibrationStatus.CalibratedHeight}, not this
                  camera's current resolution - any bound AprilTag pose estimation will be wrong until it's redone.
                </span>
              </div>
            )}

            <div>
              <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">
                Resolution / FPS {currentMode && !currentMode.IsNative && <span className="text-yellow-500">(substituted)</span>}
              </label>
              {cameraModes.length > 0 ? (
                <select
                  value={currentMode ? modeKey(currentMode) : ''}
                  onChange={e => {
                    const mode = cameraModes.find(m => modeKey(m) === e.target.value);
                    if (mode) changeMode(mode);
                  }}
                  className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white"
                >
                  {currentMode && !cameraModes.some(m => modeKey(m) === modeKey(currentMode!)) && (
                    <option value={modeKey(currentMode)}>{modeLabel(currentMode)} (current)</option>
                  )}
                  {cameraModes.map(m => (
                    <option key={modeKey(m)} value={modeKey(m)}>{modeLabel(m)}</option>
                  ))}
                </select>
              ) : (
                <p className="text-xs text-gray-400">
                  {currentMode ? modeLabel(currentMode) : 'Loading modes...'}
                  {cameraModes.length === 0 && currentMode && ' - device reports no other selectable modes'}
                </p>
              )}
            </div>

            <div className="flex items-center justify-between">
              <span className="text-xs text-gray-700 dark:text-gray-300">Auto Exposure</span>
              <ToggleSwitch enabled={autoExposure} onChange={changeAutoExposure} />
            </div>

            {/* a short fixed exposure keeps AprilTag detection reliable by limiting motion blur */}
            <div className={autoExposure ? 'opacity-50 pointer-events-none' : ''}>
              {/* units are camera-specific, so show the device's real range */}
              <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">
                Exposure{cameraControls?.Exposure.Supported
                  ? ` (${cameraControls.Exposure.Minimum}-${cameraControls.Exposure.Maximum}, default ${cameraControls.Exposure.Default})`
                  : cameraControls ? ' (not supported by this camera)' : ''}
              </label>
              <div className="flex gap-2">
                <input type="number" value={exposureValue} onChange={e => setExposureValue(parseInt(e.target.value) || 0)}
                  min={cameraControls?.Exposure.Supported ? cameraControls.Exposure.Minimum : undefined}
                  max={cameraControls?.Exposure.Supported ? cameraControls.Exposure.Maximum : undefined}
                  step={cameraControls?.Exposure.Supported ? cameraControls.Exposure.Step : undefined}
                  className="flex-1 px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                <button onClick={applyExposure} className="px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700">Apply</button>
              </div>
            </div>

            <div>
              <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">
                Gain{cameraControls?.Gain.Supported
                  ? ` (${cameraControls.Gain.Minimum}-${cameraControls.Gain.Maximum}, default ${cameraControls.Gain.Default})`
                  : cameraControls ? ' (not supported by this camera)' : ''}
              </label>
              <div className="flex gap-2">
                <input type="number" value={gainValue} onChange={e => setGainValue(parseInt(e.target.value) || 0)}
                  min={cameraControls?.Gain.Supported ? cameraControls.Gain.Minimum : undefined}
                  max={cameraControls?.Gain.Supported ? cameraControls.Gain.Maximum : undefined}
                  step={cameraControls?.Gain.Supported ? cameraControls.Gain.Step : undefined}
                  className="flex-1 px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                <button onClick={applyGain} className="px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700">Apply</button>
              </div>
            </div>

            <p className="text-xs text-gray-400">
              Not every device/driver honours all of these - a control this camera doesn't support fails with a toast rather than silently doing nothing.
            </p>
          </div>
        )}

        {sink && (
          <>
            <div className="flex items-center justify-between">
              <span className="text-sm text-gray-700 dark:text-gray-300">Enabled</span>
              <ToggleSwitch enabled={isRunning ?? false} onChange={toggleEnabled} />
            </div>

            {/* sets the backend directly on the sink (rebuilds the detector in place) */}
            {isApriltagSink && (
              <div>
                <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Backend</label>
                <select
                  value={sinkBackend ?? 0}
                  disabled={sinkBackend === null || switchingBackend}
                  onChange={e => switchBackend(parseInt(e.target.value))}
                  className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white disabled:opacity-50"
                >
                  <option value={0}>CPU (apriltag)</option>
                  <option value={1}>Vulkan (vkapriltag)</option>
                </select>
              </div>
            )}

            {/* Threads maps to libapriltag nthreads / vkapriltag cpu_threads; Vulkan decimation is integer-only
                and must divide the frame size; the refine method is chosen on Vulkan only (the CPU backend always runs upstream's). */}
            {isApriltagSink && (
              <div className="space-y-2">
                <div>
                  <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Threads (0 = default)</label>
                  <input type="number" min={0} value={threadsValue} onChange={e => setThreadsValue(parseInt(e.target.value) || 0)}
                    className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                </div>
                {quadDecimateSupported && (
                  <div>
                    <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">
                      Decimation (0 = default{sinkBackend === 1 ? ', whole numbers only on Vulkan' : ''})
                    </label>
                    <input type="number" min={0} step={sinkBackend === 1 ? 1 : 0.5} value={quadDecimateValue}
                      onChange={e => setQuadDecimateValue(parseFloat(e.target.value) || 0)}
                      className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                  </div>
                )}
                <label className="flex items-center gap-2 text-xs text-gray-700 dark:text-gray-300"
                  title="Gradient-based corner refinement - more accurate corners/pose, costs some CPU. Recommended when decimating.">
                  <input type="checkbox" checked={refineEdgesValue} onChange={e => setRefineEdgesValue(e.target.checked)} />
                  Refine edges
                </label>
                <div>
                  <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Refine edges method</label>
                  <select value={refineModeValue} disabled={!refineEdgesValue || !refineModeSupported}
                    title={refineModeSupported ? 'Exact matches upstream bit for bit; Fast and Ultra-fast trade a little accuracy for speed.' : 'Only the Vulkan backend can choose a refine method; the CPU backend always uses upstream\'s.'}
                    onChange={e => setRefineModeValue(parseInt(e.target.value))}
                    className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white disabled:opacity-50">
                    {REFINE_EDGES_MODES.map(m => <option key={m.value} value={m.value}>{m.label}</option>)}
                  </select>
                </div>
                <button onClick={applyTuning} disabled={applyingTuning}
                  className="w-full px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700 disabled:opacity-50">Apply Tuning</button>
              </div>
            )}

            {/* read-only: backend is fixed by the uploaded model's file format */}
            {isObjectDetectionSink && (
              <div className="flex items-center justify-between">
                <span className="text-xs text-gray-700 dark:text-gray-300">Backend</span>
                <span className="text-xs text-gray-500 dark:text-gray-400">{detectionBackendName ?? 'Loading...'}</span>
              </div>
            )}

            <div className="flex items-center justify-between">
              <span className="text-sm text-gray-700 dark:text-gray-300 flex items-center gap-1"><Radio className="w-3 h-3" />Publish to NT4</span>
              <ToggleSwitch enabled={nt4Sink?.IsRunning ?? false} onChange={toggleNT4} />
            </div>

            <div>
              <button onClick={fetchResult} className="text-xs text-blue-600 hover:text-blue-700 flex items-center gap-1">
                <Code className="w-3 h-3" />{resultJson === null ? 'Fetch latest result' : 'Refresh result'}
              </button>
              {resultJson !== null && (
                <pre className="mt-2 p-2 text-xs bg-gray-100 dark:bg-gray-900 rounded overflow-x-auto max-h-64">{resultJson}</pre>
              )}
            </div>
          </>
        )}

        {/* Live Preview binds a WebRTCSink to whichever node is selected (Source or Sink) */}
        {previewTarget && (
          <div>
            <div className="flex items-center justify-between">
              <span className="text-sm text-gray-700 dark:text-gray-300">Live Preview</span>
              <button onClick={togglePreview} className={`px-2 py-1 rounded text-xs flex items-center gap-1 text-white ${webrtcSink?.IsRunning ? 'bg-red-600 hover:bg-red-700' : 'bg-green-600 hover:bg-green-700'}`}>
                {webrtcSink?.IsRunning ? <><Square className="w-3 h-3" />Stop</> : <><Play className="w-3 h-3" />Start</>}
              </button>
            </div>
            {webrtcSink?.IsRunning && (
              <StreamView sinkId={webrtcSink.Sink.Id} sourceId={previewTarget.Id} onStop={togglePreview} onError={() => onToast('Preview stream error', 'error')} />
            )}
          </div>
        )}

        {/* Recording: segmented MP4 plus a JSON-Lines telemetry sidecar per segment */}
        {previewTarget && (
          <div>
            <div className="flex items-center justify-between">
              <span className="text-sm text-gray-700 dark:text-gray-300 flex items-center gap-1"><Circle className="w-3 h-3" />Recording</span>
              <button onClick={toggleRecording} className={`px-2 py-1 rounded text-xs flex items-center gap-1 text-white ${recordSink?.IsRunning ? 'bg-red-600 hover:bg-red-700' : 'bg-green-600 hover:bg-green-700'}`}>
                {recordSink?.IsRunning ? <><Square className="w-3 h-3" />Stop</> : <><Play className="w-3 h-3" />Start</>}
              </button>
            </div>
            {recordSink && segments.length > 0 && (
              <div className="mt-2 space-y-1">
                {segments.map(seg => (
                  <div key={seg.FileName} className="flex items-center justify-between text-xs bg-gray-50 dark:bg-gray-700 rounded px-2 py-1 gap-2">
                    <span className="truncate flex-1" title={seg.FileName}>{seg.FileName}</span>
                    <span className="text-gray-500 dark:text-gray-400 whitespace-nowrap">{(seg.SizeBytes / (1024 * 1024)).toFixed(1)} MB</span>
                    <a href={recordSink ? api.getRecordSinkDownloadUrl(recordSink.Sink.Id, seg.FileName) : '#'}
                      className="text-blue-600 hover:text-blue-700" title="Download"><Download className="w-3.5 h-3.5" /></a>
                    <button onClick={() => promoteSegment(seg.FileName)} disabled={promotingSegment === seg.FileName}
                      className="text-blue-600 hover:text-blue-700 disabled:opacity-50" title="Use as a Video File source">
                      <FolderInput className="w-3.5 h-3.5" />
                    </button>
                    <button onClick={() => deleteSegment(seg.FileName)} className="text-red-600 hover:text-red-700" title="Delete">
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  </div>
                ))}
              </div>
            )}
            {recordSink && segments.length === 0 && (
              <p className="mt-1 text-xs text-gray-400">No segments recorded yet.</p>
            )}
          </div>
        )}

        {source && source.Profiles.length >= 0 && (
          <div className="pt-2 border-t border-gray-200 dark:border-gray-700">
            <h4 className="text-xs font-medium text-gray-500 dark:text-gray-400 mb-2">Pipeline Profiles</h4>
            {source.Profiles.length === 0 ? (
              <p className="text-xs text-gray-400 mb-2">No profiles yet - a plain-created detector sink still works, profiles are only needed to switch between configurations at runtime.</p>
            ) : (
              <div className="space-y-1 mb-2">
                {source.Profiles.map(p => (
                  <div key={p.Index} className="flex items-center justify-between text-xs bg-gray-50 dark:bg-gray-700 rounded px-2 py-1">
                    <span>{p.Name} {p.TagSize != null && `(${p.TagSize}m)`}</span>
                    {source.ActiveProfileIndex === p.Index ? (
                      <span className="text-green-600 dark:text-green-400 font-medium">active</span>
                    ) : (
                      <button onClick={() => activateProfile(p.Index)} className="text-blue-600 hover:text-blue-700 flex items-center gap-1"><RefreshCw className="w-3 h-3" />Activate</button>
                    )}
                  </div>
                ))}
              </div>
            )}
            <div className="space-y-1">
              <input value={newProfileName} onChange={e => setNewProfileName(e.target.value)} placeholder="New AprilTag profile name"
                className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
              <div className="flex gap-2">
                <input type="number" step="any" value={newProfileTagSize} onChange={e => setNewProfileTagSize(parseFloat(e.target.value) || 0.1651)}
                  title="Tag size (meters)"
                  className="w-20 px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                <select value={newProfileBackend} onChange={e => setNewProfileBackend(parseInt(e.target.value))}
                  className="flex-1 px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white">
                  <option value={0}>CPU (apriltag)</option>
                  <option value={1}>Vulkan (vkapriltag)</option>
                </select>
                <button onClick={createApriltagProfile} className="px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700">Add</button>
              </div>
            </div>
          </div>
        )}

        <div className="pt-2 border-t border-gray-200 dark:border-gray-700">
          <button onClick={handleDelete} className="px-3 py-2 bg-red-600 text-white rounded text-sm hover:bg-red-700 flex items-center gap-2 w-full justify-center">
            <Trash2 className="w-4 h-4" />Delete
          </button>
        </div>
      </div>
    </div>
  );
};
