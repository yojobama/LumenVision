import React, { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Camera, Wand2 } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import type {
  Source, Sink, StereoDepthStats, StoredStereoCalibration,
} from '../types';
import { StereoDepthBackendKind, StereoFrameOutput, STEREO_BACKEND_LABELS, STEREO_FRAME_OUTPUT_LABELS } from '../types';

const api = new ApiService();

type Props = {
  sources: Source[];
  sinks: Sink[];
  onToast: (message: string, type: 'success' | 'error' | 'info') => void;
  onRefresh: () => void;
};

const cameraSources = (sources: Source[]) => sources.filter(s => s.type?.toLowerCase().includes('camera'));

const CameraSelect: React.FC<{
  sources: Source[]; value: number | ''; onChange: (id: number | '') => void; label: string;
}> = ({ sources, value, onChange, label }) => (
  <label className="block text-sm">
    <span className="text-gray-600 dark:text-gray-400">{label}</span>
    <select
      className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white"
      value={value}
      onChange={e => onChange(e.target.value ? Number(e.target.value) : '')}
    >
      <option value="">Select a camera…</option>
      {cameraSources(sources).map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
    </select>
  </label>
);

// Stereo calibration lives in the Calibration tab; this points there.
const StereoCalibrationCard: React.FC = () => (
  <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
    <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-1">1. Stereo Calibration</h3>
    <p className="text-sm text-gray-500 dark:text-gray-400 mb-4">
      Calibrate the stereo pair in the Calibration tab, then pick the saved result below.
    </p>
    <Link to="/calibration" className="inline-flex items-center gap-2 px-4 py-2 bg-purple-600 hover:bg-purple-700 text-white rounded text-sm font-medium">
      <Wand2 className="w-4 h-4" />Open the Calibration tab
    </Link>
  </div>
);

const StereoDepthCard: React.FC<{
  sources: Source[]; sinks: Sink[]; onToast: Props['onToast']; onRefresh: () => void;
}> = ({ sources, sinks, onToast, onRefresh }) => {
  const depthSinks = sinks.filter(s => s.type === 'stereodepth');
  const [savedCalibrations, setSavedCalibrations] = useState<StoredStereoCalibration[]>([]);
  const [name, setName] = useState('Stereo Depth');
  const [backend, setBackend] = useState<number>(StereoDepthBackendKind.SGBM);
  const [minDepth, setMinDepth] = useState(0.5);
  const [maxDepth, setMaxDepth] = useState(6.0);
  const [maxSkewUs, setMaxSkewUs] = useState(33000);
  const [frameOutput, setFrameOutput] = useState<number>(StereoFrameOutput.DEPTH_COLORMAP);
  const [calibrationIndex, setCalibrationIndex] = useState<number | ''>('');
  const [left, setLeft] = useState<number | ''>('');
  const [right, setRight] = useState<number | ''>('');
  const [busy, setBusy] = useState(false);
  const [stats, setStats] = useState<Record<number, StereoDepthStats & { backendName: string }>>({});

  useEffect(() => {
    api.getSavedStereoCalibrations().then(setSavedCalibrations).catch(() => setSavedCalibrations([]));
  }, []);

  const create = async () => {
    if (!left || !right) { onToast('Select both cameras first', 'error'); return; }
    const calibration = calibrationIndex === '' ? undefined : savedCalibrations[calibrationIndex]?.Result;
    if (!calibration || !calibration.Q?.length || calibration.BaselineMeters <= 0) {
      onToast('Select a calibration that has actually been run (baselineMeters > 0)', 'error');
      return;
    }
    setBusy(true);
    try {
      const id = await api.createStereoDepthSink({
        name, backend, minDepthMeters: minDepth, maxDepthMeters: maxDepth,
        maxSkewUs, frameOutput, calibration,
      });
      await api.bindStereoDepthSources(id, left as number, right as number);
      onToast(`Stereo depth sink #${id} created and bound`, 'success');
      onRefresh();
    } catch (e) {
      onToast(`Failed to create stereo depth sink: ${e}`, 'error');
    } finally {
      setBusy(false);
    }
  };

  const refreshStats = async (sinkId: number) => {
    try {
      const [s, backendName] = await Promise.all([api.getStereoDepthStats(sinkId), api.getStereoDepthBackendName(sinkId)]);
      setStats(prev => ({ ...prev, [sinkId]: { ...s, backendName } }));
    } catch (e) {
      onToast(`Failed to fetch stats: ${e}`, 'error');
    }
  };

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
      <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-1">2. Stereo Depth</h3>
      <p className="text-sm text-gray-500 dark:text-gray-400 mb-4">
        Pick a calibration that's actually been run, a backend, and a depth range. STEREO_BACKEND_SGBM always works;
        the codec-stereo backends need <code>LUMEN_WITH_CODEC_STEREO</code> (on by default).
      </p>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-3 mb-4">
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Name</span>
          <input className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={name} onChange={e => setName(e.target.value)} />
        </label>
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Calibration</span>
          <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={calibrationIndex} onChange={e => setCalibrationIndex(e.target.value ? Number(e.target.value) : '')}>
            <option value="">Select a saved calibration…</option>
            {savedCalibrations.map((c, i) => (
              <option key={i} value={i}>
                {c.Result.ImageWidth}×{c.Result.ImageHeight} · epipolar {c.Result.EpipolarRms.toFixed(3)}px · {new Date(c.CalibratedAtUnixMs).toLocaleDateString()}
              </option>
            ))}
          </select>
        </label>
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Backend</span>
          <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={backend} onChange={e => setBackend(Number(e.target.value))}>
            {Object.entries(STEREO_BACKEND_LABELS).map(([v, label]) => <option key={v} value={v}>{label}</option>)}
          </select>
        </label>
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Frame output</span>
          <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={frameOutput} onChange={e => setFrameOutput(Number(e.target.value))}>
            {Object.entries(STEREO_FRAME_OUTPUT_LABELS).map(([v, label]) => <option key={v} value={v}>{label}</option>)}
          </select>
        </label>
        <div className="grid grid-cols-2 gap-2">
          <label className="block text-sm">
            <span className="text-gray-600 dark:text-gray-400">Min depth (m)</span>
            <input type="number" step="0.1" className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={minDepth} onChange={e => setMinDepth(Number(e.target.value))} />
          </label>
          <label className="block text-sm">
            <span className="text-gray-600 dark:text-gray-400">Max depth (m)</span>
            <input type="number" step="0.1" className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={maxDepth} onChange={e => setMaxDepth(Number(e.target.value))} />
          </label>
        </div>
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Max capture skew (µs)</span>
          <input type="number" className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={maxSkewUs} onChange={e => setMaxSkewUs(Number(e.target.value))} />
        </label>
        <CameraSelect sources={sources} value={left} onChange={setLeft} label="Left camera" />
        <CameraSelect sources={sources} value={right} onChange={setRight} label="Right camera" />
      </div>
      <button disabled={busy} onClick={create} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium">
        Create stereo depth sink
      </button>

      {depthSinks.length > 0 && (
        <div className="mt-6 space-y-3">
          {depthSinks.map(sink => {
            const s = stats[sink.id];
            return (
              <div key={sink.id} className="border border-gray-200 dark:border-gray-700 rounded p-4">
                <div className="flex items-center justify-between">
                  <div className="font-medium text-gray-900 dark:text-white">{sink.name} <span className="text-gray-400">#{sink.id}</span></div>
                  <button onClick={() => refreshStats(sink.id)} className="px-3 py-1 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-xs font-medium">Refresh stats</button>
                </div>
                <div className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  left #{sink.sourceId ?? '-'}, right #{sink.source2Id ?? '-'}
                </div>
                {s && (
                  <div className="mt-2 text-sm text-gray-700 dark:text-gray-300">
                    backend={s.backendName} · valid={(s.ValidFraction * 100).toFixed(0)}% · median depth={s.MedianDepthMeters.toFixed(2)}m
                  </div>
                )}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};

const DepthFusionCard: React.FC<{
  sinks: Sink[]; onToast: Props['onToast']; onRefresh: () => void;
  onBindDetectorToDepthFrame: (detectorId: number, depthSinkId: number) => Promise<void>;
}> = ({ sinks, onToast, onRefresh, onBindDetectorToDepthFrame }) => {
  const fusionSinks = sinks.filter(s => s.type === 'depthfusion');
  const depthSinks = sinks.filter(s => s.type === 'stereodepth');
  const detectorSinks = sinks.filter(s => s.type === 'apriltag' || s.type === 'object');
  const [name, setName] = useState('Depth Fusion');
  const [detectorId, setDetectorId] = useState<number | ''>('');
  const [depthSinkId, setDepthSinkId] = useState<number | ''>('');
  const [busy, setBusy] = useState(false);

  const create = async () => {
    if (!detectorId || !depthSinkId) { onToast('Select both a detector and a stereo depth sink', 'error'); return; }
    setBusy(true);
    try {
      // the detector must run on the depth sink's rectified-left output so its bbox pixels index the depth grid
      await onBindDetectorToDepthFrame(detectorId as number, depthSinkId as number);
      const id = await api.createDepthFusionSink(name);
      await api.bindSinkToSource(id, detectorId as number);
      await api.attachDepthFusionSource(id, depthSinkId as number);
      onToast(`Depth fusion sink #${id} created`, 'success');
      onRefresh();
    } catch (e) {
      onToast(`Failed to create depth fusion sink: ${e}`, 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
      <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-1">3. Depth Fusion (optional)</h3>
      <p className="text-sm text-gray-500 dark:text-gray-400 mb-4">
        Fuses a detector's bounding boxes with a stereo depth grid for a real-world distance -
        "note at 2.4m, 15° left" instead of just a pixel box. Rebinds the chosen detector onto
        the depth sink's rectified-left output automatically.
      </p>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-3 mb-4">
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Name</span>
          <input className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={name} onChange={e => setName(e.target.value)} />
        </label>
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Detector (AprilTag/Object detection)</span>
          <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={detectorId} onChange={e => setDetectorId(e.target.value ? Number(e.target.value) : '')}>
            <option value="">Select a detector…</option>
            {detectorSinks.map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
          </select>
        </label>
        <label className="block text-sm">
          <span className="text-gray-600 dark:text-gray-400">Stereo depth sink</span>
          <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={depthSinkId} onChange={e => setDepthSinkId(e.target.value ? Number(e.target.value) : '')}>
            <option value="">Select a stereo depth sink…</option>
            {depthSinks.map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
          </select>
        </label>
      </div>
      <button disabled={busy} onClick={create} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium">
        Create depth fusion sink
      </button>

      {fusionSinks.length > 0 && (
        <div className="mt-6 space-y-2">
          {fusionSinks.map(sink => (
            <div key={sink.id} className="border border-gray-200 dark:border-gray-700 rounded p-3 text-sm text-gray-700 dark:text-gray-300">
              {sink.name} <span className="text-gray-400">#{sink.id}</span> - detector #{sink.sourceId ?? '-'}
            </div>
          ))}
        </div>
      )}
    </div>
  );
};

export const StereoPage: React.FC<Props> = ({ sources, sinks, onToast, onRefresh }) => {
  const bindDetectorToDepthFrame = async (detectorId: number, depthSinkId: number) => {
    // the depth sink is dual-role (ISink and ISource), so its id is a valid bind target
    await api.bindSinkToSource(detectorId, depthSinkId);
  };

  return (
    <div className="space-y-6">
      <div className="bg-blue-50 dark:bg-blue-900/30 border border-blue-200 dark:border-blue-800 rounded-lg p-4 flex items-start gap-3">
        <Camera className="w-5 h-5 text-blue-500 mt-0.5 flex-shrink-0" />
        <p className="text-sm text-blue-800 dark:text-blue-200">
          See <code>STEREO_IMPLEMENTATION_PLAN.md</code> for the full design - camera synchronization,
          depth accuracy expectations, and disparity range derivation are all worth reading before
          buying stereo hardware. Do calibration first; a depth node created against a calibration
          that hasn't been run yet will produce a mostly-empty depth map with no other symptom.
        </p>
      </div>

      <StereoCalibrationCard />
      <StereoDepthCard sources={sources} sinks={sinks} onToast={onToast} onRefresh={onRefresh} />
      <DepthFusionCard sinks={sinks} onToast={onToast} onRefresh={onRefresh} onBindDetectorToDepthFrame={bindDetectorToDepthFrame} />
    </div>
  );
};

export default StereoPage;
