import React, { useEffect, useState } from 'react';
import { X, Trash2, Wifi, WifiOff, Radio, Play, Square, Code, RefreshCw, AlertTriangle, Circle, Download, FolderInput } from 'lucide-react';
import type { PipelineNode } from './model';
import type { WsSource, WsSink, NT4Defaults, CameraMode, CameraControls, CalibrationStatus, RecordSegment, ApriltagAdvancedSettings, Model } from '../types';
import { REFINE_EDGES_MODES, APRILTAG_FAMILIES, DEFAULT_APRILTAG_ADVANCED, PipelineProfileKind } from '../types';
import { ApiService } from '../services/ApiService';
import { ToggleSwitch } from '../components/ToggleSwitch';
import { CameraControlsPanel } from '../components/CameraControlsPanel';
import { CameraTransformPanel } from '../components/CameraTransformPanel';
import { FieldLayoutPicker } from '../components/FieldLayoutPicker';
import { ThresholdFields } from '../components/ThresholdFields';
import { StreamSettingsPanel } from '../components/StreamSettingsPanel';
import { deleteNode } from './nodeActions';
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
  const { kind, raw, webrtcSink, mjpegSink, nt4Sink, recordSink, isRunning } = node.data;
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
  const [driverMode, setDriverMode] = useState(false);
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
  const [detectionThresholds, setDetectionThresholds] = useState<{ confThreshold: number; nmsThreshold: number } | null>(null);
  const [models, setModels] = useState<Model[]>([]);
  const [newObjectProfileName, setNewObjectProfileName] = useState('');
  const [newObjectProfileModel, setNewObjectProfileModel] = useState<number | ''>('');
  const [newObjectProfileThresholds, setNewObjectProfileThresholds] = useState<{ confThreshold: number; nmsThreshold: number } | null>(null);
  const [advanced, setAdvanced] = useState<ApriltagAdvancedSettings>(DEFAULT_APRILTAG_ADVANCED);
  const [quadSigmaSupported, setQuadSigmaSupported] = useState(true);
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

  const toggleDriverMode = async (enabled: boolean) => {
    if (!sink) return;
    try {
      await api.setDriverMode(sink.Id, enabled);
      setDriverMode(enabled);
    } catch {
      onToast('Failed to change driver mode', 'error');
    }
  };

  const takeSnapshot = async (kind: 'input' | 'output') => {
    if (!source) return;
    try {
      const saved = await api.takeSnapshot(source.Id, kind);
      onToast(saved ? `Snapshot saved: ${saved}` : 'No frame available yet', saved ? 'success' : 'error');
    } catch {
      onToast('Failed to take a snapshot', 'error');
    }
  };

  const isApriltagSink = sink != null && node.data.typeName === 'ApriltagSink';
  const isObjectDetectionSink = sink != null && node.data.typeName === 'ObjectDetectionSink';
  const [detectionBackendName, setDetectionBackendName] = useState<string | null>(null);

  // read-only: no backend switcher for object detection
  useEffect(() => {
    if (!isObjectDetectionSink || !sink) return;
    let cancelled = false;
    api.getObjectDetectionThresholds(sink.Id)
      .then(t => { if (!cancelled) setDetectionThresholds({ confThreshold: t.confThreshold, nmsThreshold: t.nmsThreshold }); })
      .catch(() => { if (!cancelled) setDetectionThresholds(null); });
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
    api.getDriverMode(sink.Id)
      .then(enabled => { if (!cancelled) setDriverMode(enabled); })
      .catch(() => { /* a detector that cannot report driver mode keeps the toggle off */ });
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
        setAdvanced(tuning.advanced);
        setQuadSigmaSupported(tuning.quadSigmaSupported);
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
      await api.setApriltagBackend(sink.Id, backend, threadsValue, quadDecimateValue, refineEdgesValue, refineModeValue, advanced);
      setSinkBackend(backend);
      const actual = await api.getApriltagTuning(sink.Id);
      setRefineModeValue(actual.refineMode);
      setRefineModeSupported(actual.refineModeSupported);
      setAdvanced(actual.advanced);
      setQuadSigmaSupported(actual.quadSigmaSupported);
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
      await api.setApriltagBackend(sink.Id, sinkBackend, threadsValue, quadDecimateSupported ? quadDecimateValue : undefined, refineEdgesValue, refineModeValue, advanced);
      const actual = await api.getApriltagTuning(sink.Id);
      setAdvanced(actual.advanced);
      setQuadSigmaSupported(actual.quadSigmaSupported);
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
    try {
      if (!(await deleteNode(node))) return;
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

  // models for the "add object detection pipeline" form
  useEffect(() => {
    if (!isCamera) return;
    api.getAllModels()
      .then(list => { setModels(list); setNewObjectProfileModel(current => (current === '' && list.length > 0 ? list[0].id : current)); })
      .catch(() => setModels([]));
  }, [node.id, isCamera]);

  const applyDetectionThresholds = async () => {
    if (!sink || !detectionThresholds) return;
    try {
      await api.setObjectDetectionThresholds(sink.Id, detectionThresholds.confThreshold, detectionThresholds.nmsThreshold);
      onToast('Cutoffs applied', 'success');
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to apply the cutoffs', 'error');
    }
  };

  const createObjectDetectionProfile = async () => {
    if (!source || !newObjectProfileName.trim() || newObjectProfileModel === '') return;
    try {
      await api.createObjectDetectionProfile(source.Id, newObjectProfileName.trim(), newObjectProfileModel, newObjectProfileThresholds ?? undefined);
      setNewObjectProfileName('');
      onToast('Object detection pipeline created', 'success');
    } catch {
      onToast('Failed to create the pipeline', 'error');
    }
  };

  const toggleCameraOverrides = async (index: number, enabled: boolean) => {
    if (!source) return;
    try {
      await api.setProfileCameraOverrides(source.Id, index, enabled);
      onToast(enabled ? 'This pipeline now keeps its own camera settings' : 'This pipeline uses the camera settings again', 'success');
    } catch {
      onToast("Failed to change the pipeline's camera settings", 'error');
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
            <input id="inspector-name" value={name} onChange={e => setName(e.target.value)} onBlur={saveName}
              onKeyDown={e => {
                if (e.key === 'Enter') e.currentTarget.blur();
                if (e.key === 'Escape') { setName(node.data.label); e.currentTarget.blur(); }
              }}
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

            <details>
              <summary className="text-xs text-gray-500 dark:text-gray-400 cursor-pointer select-none">Rotate, mirror and crop</summary>
              <div className="mt-2">
                <CameraTransformPanel sourceId={source.Id} onToast={onToast} />
              </div>
            </details>

            <details>
              <summary className="text-xs text-gray-500 dark:text-gray-400 cursor-pointer select-none">All camera controls</summary>
              <div className="mt-2">
                <CameraControlsPanel sourceId={source.Id} onToast={onToast} />
              </div>
            </details>

            <div className="flex gap-2">
              <button onClick={() => takeSnapshot('input')} className="flex-1 px-2 py-1 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-xs">
                Input snapshot
              </button>
              <button onClick={() => takeSnapshot('output')} className="flex-1 px-2 py-1 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-xs"
                title="The active detector's annotated frame (the raw frame when no detector runs)">
                Output snapshot
              </button>
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

            {isApriltagSink && (
              <div className="flex items-center justify-between" title="Driver mode streams the raw camera image and skips detection and result publishing.">
                <span className="text-sm text-gray-700 dark:text-gray-300">Driver mode</span>
                <ToggleSwitch enabled={driverMode} onChange={toggleDriverMode} />
              </div>
            )}

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
                <FieldLayoutPicker resetKey={node.id} onToast={onToast}
                  loadTagCount={() => api.getSinkFieldLayoutTagCount(sink!.Id)}
                  applyBundled={layout => api.setSinkBundledFieldLayout(sink!.Id, layout)}
                  applyUpload={json => api.uploadSinkFieldLayout(sink!.Id, json)} />
                <div className="space-y-2 pt-2 border-t border-gray-200 dark:border-gray-700">
                  <div>
                    <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Tag family</label>
                    <select value={advanced.family} onChange={e => setAdvanced({ ...advanced, family: parseInt(e.target.value) })}
                      className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white">
                      {APRILTAG_FAMILIES.map(f => <option key={f.value} value={f.value}>{f.label}</option>)}
                    </select>
                  </div>
                  <div className="grid grid-cols-2 gap-2">
                    <label className="text-xs text-gray-500 dark:text-gray-400"
                      title={quadSigmaSupported ? 'Gaussian blur before the quad search; helps with noisy images, 0 = none.' : 'The Vulkan backend has no blur stage.'}>
                      Blur (sigma)
                      <input type="number" min={0} step={0.1} value={advanced.quadSigma} disabled={!quadSigmaSupported}
                        onChange={e => setAdvanced({ ...advanced, quadSigma: Math.max(0, parseFloat(e.target.value) || 0) })}
                        className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white disabled:opacity-50" />
                    </label>
                    <label className="text-xs text-gray-500 dark:text-gray-400" title="How many corrupted bits a tag may have and still be read. More finds more tags, and more false ones.">
                      Max hamming
                      <select value={advanced.maxHamming} onChange={e => setAdvanced({ ...advanced, maxHamming: parseInt(e.target.value) })}
                        className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white">
                        {[0, 1, 2].map(bits => <option key={bits} value={bits}>{bits} bit{bits === 1 ? '' : 's'}</option>)}
                      </select>
                    </label>
                    <label className="text-xs text-gray-500 dark:text-gray-400" title="Detections with a lower decision margin are dropped. 0 keeps everything; a clean tag scores well over 50.">
                      Decision margin
                      <input type="number" min={0} step={1} value={advanced.decisionMargin}
                        onChange={e => setAdvanced({ ...advanced, decisionMargin: Math.max(0, parseFloat(e.target.value) || 0) })}
                        className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                    </label>
                    <label className="text-xs text-gray-500 dark:text-gray-400" title="Steps of the per-tag pose refinement. More is slower and rarely changes the answer.">
                      Pose iterations
                      <input type="number" min={1} max={500} step={1} value={advanced.poseIterations}
                        onChange={e => setAdvanced({ ...advanced, poseIterations: Math.min(500, Math.max(1, parseInt(e.target.value) || 50)) })}
                        className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                    </label>
                  </div>
                  <label className="flex items-center gap-2 text-xs text-gray-700 dark:text-gray-300" title="Solve one robot-relative pose from every visible tag with a known field position (needs a field layout and a calibration).">
                    <input type="checkbox" checked={advanced.multiTag} onChange={e => setAdvanced({ ...advanced, multiTag: e.target.checked })} />
                    Multi-tag pose
                  </label>
                  <label className="flex items-center gap-2 text-xs text-gray-700 dark:text-gray-300" title="Estimate a pose for each tag by itself. Off publishes only where each tag is in the image.">
                    <input type="checkbox" checked={advanced.singleTagPose} onChange={e => setAdvanced({ ...advanced, singleTagPose: e.target.checked })} />
                    Single-tag poses
                  </label>
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
            {isObjectDetectionSink && detectionThresholds && (
              <div className="space-y-2">
                <ThresholdFields {...detectionThresholds} onChange={setDetectionThresholds} />
                <button onClick={applyDetectionThresholds}
                  className="w-full px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700">Apply cutoffs</button>
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
            {(webrtcSink || mjpegSink) && (
              <details className="mt-2">
                <summary className="text-xs text-gray-500 dark:text-gray-400 cursor-pointer select-none">Stream quality</summary>
                <div className="mt-2">
                  <StreamSettingsPanel webrtc={webrtcSink?.Sink} mjpeg={mjpegSink?.Sink} onToast={onToast} />
                </div>
              </details>
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
                  <div key={p.Index} className="text-xs bg-gray-50 dark:bg-gray-700 rounded px-2 py-1">
                  <div className="flex items-center justify-between gap-2">
                    <span className="flex-1 truncate">{p.Name} {p.TagSize != null && `(${p.TagSize}m)`}
                      {p.Kind === PipelineProfileKind.ObjectDetectionSink && p.ModelId != null && ` (${models.find(m => m.id === p.ModelId)?.name ?? `model ${p.ModelId}`})`}</span>
                    {isCamera && (
                      <label className="flex items-center gap-1 text-gray-500 dark:text-gray-400 whitespace-nowrap"
                        title="Give this pipeline its own camera controls, rotation/crop and FPS limit. While it is the active pipeline, camera edits belong to it; the camera's own settings come back for the others.">
                        <input type="checkbox" checked={p.CameraOverrides != null} onChange={e => toggleCameraOverrides(p.Index, e.target.checked)} />
                        own camera settings
                      </label>
                    )}
                    {source.ActiveProfileIndex === p.Index ? (
                      <span className="text-green-600 dark:text-green-400 font-medium">active</span>
                    ) : (
                      <button onClick={() => activateProfile(p.Index)} className="text-blue-600 hover:text-blue-700 flex items-center gap-1"><RefreshCw className="w-3 h-3" />Activate</button>
                    )}
                  </div>
                  {p.Kind === PipelineProfileKind.ApriltagSink && (
                    <details className="mt-1">
                      <summary className="cursor-pointer select-none text-gray-500 dark:text-gray-400">Field layout</summary>
                      <div className="mt-1">
                        <FieldLayoutPicker resetKey={`${source.Id}-${p.Index}`} onToast={onToast}
                          loadTagCount={() => api.getProfileFieldLayoutTagCount(source.Id, p.Index)}
                          applyBundled={layout => api.setProfileBundledFieldLayout(source.Id, p.Index, layout)}
                          applyUpload={json => api.uploadProfileFieldLayout(source.Id, p.Index, json)} />
                      </div>
                    </details>
                  )}
                  </div>
                ))}
              </div>
            )}
            {models.length > 0 && (
              <details className="mb-2">
                <summary className="text-xs text-gray-500 dark:text-gray-400 cursor-pointer select-none">Add an object detection pipeline</summary>
                <div className="space-y-2 mt-2">
                  <input value={newObjectProfileName} onChange={e => setNewObjectProfileName(e.target.value)} placeholder="Pipeline name"
                    className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
                  <select value={newObjectProfileModel} onChange={e => { setNewObjectProfileModel(parseInt(e.target.value)); setNewObjectProfileThresholds(null); }}
                    className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white">
                    {models.map(m => <option key={m.id} value={m.id}>{m.name}</option>)}
                  </select>
                  <ThresholdFields
                    confThreshold={newObjectProfileThresholds?.confThreshold ?? models.find(m => m.id === newObjectProfileModel)?.confThreshold ?? 0.25}
                    nmsThreshold={newObjectProfileThresholds?.nmsThreshold ?? models.find(m => m.id === newObjectProfileModel)?.nmsThreshold ?? 0.45}
                    onChange={setNewObjectProfileThresholds} />
                  <button onClick={createObjectDetectionProfile} disabled={!newObjectProfileName.trim() || newObjectProfileModel === ''}
                    className="w-full px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700 disabled:opacity-50">Add pipeline</button>
                </div>
              </details>
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
