import React, { useEffect, useState, useCallback } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { ArrowLeft, Camera, CheckCircle, XCircle, Trash2, Play, RefreshCw } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import { CoverageHeatmap } from '../components/CoverageHeatmap';
import { StreamView } from '../components/StreamView';
import type { CameraCalibrationResult, CalibrationCoverage, Source } from '../types';

const api = new ApiService();

// rule-of-thumb pass/fail threshold for mono reprojection RMS (<0.5px very good, <1.0px acceptable)
const MONO_RMS_GATE = 1.0;

// Camera calibration wizard (bind, capture, run, result), reached from the Inspector's
// "Calibrate" button on a CameraCalibrationSink node.
export const CalibrationWizardPage: React.FC<{
  sources: Source[];
  onToast: (m: string, t: 'success' | 'error' | 'info') => void;
}> = ({ sources, onToast }) => {
  const { sinkId } = useParams<{ sinkId: string }>();
  const navigate = useNavigate();
  const id = Number(sinkId);

  const [boundSourceId, setBoundSourceId] = useState<number | ''>('');
  const [selectedSource, setSelectedSource] = useState<number | ''>('');
  const [snapshotCount, setSnapshotCount] = useState(0);
  const [coverage, setCoverage] = useState<CalibrationCoverage | null>(null);
  const [result, setResult] = useState<CameraCalibrationResult | null>(null);
  const [showPreview, setShowPreview] = useState(false);
  const [previewSinkId, setPreviewSinkId] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);

  const refresh = useCallback(async () => {
    try {
      const [count, cov] = await Promise.all([
        api.getCameraCalibrationSnapshotCount(id),
        api.getCameraCalibrationCoverage(id),
      ]);
      setSnapshotCount(count);
      setCoverage(cov);
    } catch {
      onToast('Failed to refresh calibration state', 'error');
    }
  }, [id]);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, 2000);
    return () => clearInterval(interval);
  }, [refresh]);

  const bind = async () => {
    if (!selectedSource) return;
    setBusy(true);
    try {
      await api.bindSinkToSource(id, selectedSource as number);
      setBoundSourceId(selectedSource);
      onToast('Camera bound', 'success');
    } catch {
      onToast('Failed to bind camera', 'error');
    } finally {
      setBusy(false);
    }
  };

  const togglePreview = async () => {
    try {
      if (previewSinkId) {
        await api.toggleSink(previewSinkId, !showPreview);
        setShowPreview(!showPreview);
        return;
      }
      const pId = await api.createWebRTCSink('calibration-preview');
      await api.bindSinkToSource(pId, id);
      await api.toggleSink(pId, true);
      setPreviewSinkId(pId);
      setShowPreview(true);
    } catch {
      onToast('Failed to start preview', 'error');
    }
  };

  const capture = async () => {
    try {
      const saved = await api.saveCameraCalibrationDetection(id);
      if (!saved) { onToast('No checkerboard detected in the latest frame - check the board is visible', 'error'); return; }
      await refresh();
      onToast(`Snapshot saved (${snapshotCount + 1} total)`, 'success');
    } catch {
      onToast('Failed to save snapshot', 'error');
    }
  };

  const clearAll = async () => {
    try {
      await api.clearCameraCalibrationSnapshots(id);
      await refresh();
      onToast('Snapshots cleared', 'info');
    } catch {
      onToast('Failed to clear snapshots', 'error');
    }
  };

  const run = async () => {
    setBusy(true);
    try {
      const r = await api.runCameraCalibration(id);
      setResult(r);
      const ok = r.Rms < MONO_RMS_GATE;
      onToast(`Calibration done: rms=${r.Rms.toFixed(3)}px ${ok ? '(good)' : '(high - capture more varied snapshots)'}`, ok ? 'success' : 'error');
    } catch {
      onToast('Calibration failed - need at least 4 snapshots', 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="fixed inset-0 bg-gray-50 dark:bg-gray-900 z-40 overflow-y-auto">
      <div className="max-w-4xl mx-auto p-6 space-y-6">
        <div className="flex items-center gap-3">
          <button onClick={() => navigate('/graph')} className="p-2 rounded-lg bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700">
            <ArrowLeft className="w-5 h-5" />
          </button>
          <div>
            <h1 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Camera className="w-5 h-5" />Camera Calibration Wizard</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">Sink #{id}</p>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 className="font-semibold text-gray-900 dark:text-white mb-3">1. Bind a camera</h2>
          <div className="flex gap-2 items-end">
            <label className="flex-1 block text-sm">
              <span className="text-gray-600 dark:text-gray-400">Camera source</span>
              <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white"
                value={selectedSource} onChange={e => setSelectedSource(e.target.value ? Number(e.target.value) : '')}>
                <option value="">Select a camera…</option>
                {sources.map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
              </select>
            </label>
            <button disabled={busy || !selectedSource} onClick={bind} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium">Bind</button>
          </div>
          {boundSourceId && <p className="text-xs text-green-600 dark:text-green-400 mt-2">Bound to source #{boundSourceId}</p>}
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 className="font-semibold text-gray-900 dark:text-white mb-3">2. Capture snapshots</h2>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <div>
              <button onClick={togglePreview} className="mb-3 px-3 py-1 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-xs font-medium">
                {showPreview ? 'Stop preview' : 'Start live preview'}
              </button>
              {showPreview && previewSinkId && (
                <StreamView sinkId={previewSinkId} sourceId={boundSourceId || null} onStop={() => setShowPreview(false)} onError={() => onToast('Preview error', 'error')} />
              )}
            </div>
            <CoverageHeatmap coverage={coverage} />
          </div>
          <div className="flex items-center gap-2 mt-4">
            <button onClick={capture} className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded text-sm font-medium">Capture snapshot</button>
            <button onClick={clearAll} className="px-3 py-2 bg-red-100 dark:bg-red-900 hover:bg-red-200 text-red-700 dark:text-red-200 rounded text-sm font-medium flex items-center gap-1"><Trash2 className="w-4 h-4" />Clear all</button>
            <span className="text-sm text-gray-600 dark:text-gray-400 ml-auto">{snapshotCount} snapshot{snapshotCount === 1 ? '' : 's'} saved (need 4+)</span>
          </div>
        </div>

        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 className="font-semibold text-gray-900 dark:text-white mb-3">3. Run &amp; result</h2>
          <button disabled={busy || snapshotCount < 4} onClick={run} className="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded text-sm font-medium flex items-center gap-2">
            <Play className="w-4 h-4" />Run calibration
          </button>
          {result && (
            <div className="mt-4 flex items-center gap-2">
              {result.Rms < MONO_RMS_GATE ? <CheckCircle className="w-5 h-5 text-green-500" /> : <XCircle className="w-5 h-5 text-red-500" />}
              <span className="text-sm text-gray-700 dark:text-gray-300">
                rms={result.Rms.toFixed(3)}px · fx={result.Fx.toFixed(1)} · fy={result.Fy.toFixed(1)} · cx={result.Cx.toFixed(1)} · cy={result.Cy.toFixed(1)} · {result.ImageWidth}×{result.ImageHeight}
              </span>
            </div>
          )}
          <button onClick={refresh} className="mt-4 text-xs text-blue-600 hover:text-blue-700 flex items-center gap-1"><RefreshCw className="w-3 h-3" />Refresh</button>
        </div>
      </div>
    </div>
  );
};
