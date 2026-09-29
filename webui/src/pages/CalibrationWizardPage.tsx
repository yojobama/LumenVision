import React, { useEffect, useState, useCallback } from 'react';
import { useParams, useNavigate, Navigate } from 'react-router-dom';
import { ArrowLeft, Camera, CheckCircle, XCircle, Trash2, Play, RefreshCw } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import { CoverageHeatmap } from '../components/CoverageHeatmap';
import { MjpegStream } from '../components/MjpegStream';
import { useCalibrationSession } from '../hooks/useCalibrationSession';
import type { CameraCalibrationResult, CalibrationCoverage } from '../types';

const api = new ApiService();

// pass/fail threshold for mono reprojection RMS (<0.5px very good, <1.0px acceptable)
const MONO_RMS_GATE = 1.0;

// Camera calibration wizard (capture, run, result) for a session started from the Calibration tab.
export const CalibrationWizardPage: React.FC<{
  onToast: (m: string, t: 'success' | 'error' | 'info') => void;
}> = ({ onToast }) => {
  const { sessionId } = useParams<{ sessionId: string }>();
  const navigate = useNavigate();
  const id = Number(sessionId);
  const state = useCalibrationSession(id, 'camera');

  const [snapshotCount, setSnapshotCount] = useState(0);
  const [coverage, setCoverage] = useState<CalibrationCoverage | null>(null);
  const [result, setResult] = useState<CameraCalibrationResult | null>(null);
  const [busy, setBusy] = useState(false);
  const ready = state.status === 'ready';

  const refresh = useCallback(async () => {
    try {
      const [count, cov] = await Promise.all([api.getCalibrationCount(id), api.getCalibrationCoverage(id)]);
      setSnapshotCount(count);
      setCoverage(cov);
    } catch {
      onToast('Failed to refresh calibration state', 'error');
    }
  }, [id, onToast]);

  useEffect(() => {
    if (!ready) return;
    refresh();
    const interval = setInterval(refresh, 2000);
    return () => clearInterval(interval);
  }, [ready, refresh]);

  if (state.status === 'missing') return <Navigate to="/calibration" replace />;
  if (state.status === 'loading') return <p className="text-sm text-gray-500 dark:text-gray-400">Loading calibration session…</p>;

  const capture = async () => {
    try {
      const saved = await api.saveCalibrationDetection(id);
      if (!saved) { onToast('No board detected in the latest frame - check the board is visible', 'error'); return; }
      await refresh();
      onToast('Snapshot saved', 'success');
    } catch {
      onToast('Failed to save snapshot', 'error');
    }
  };

  const clearAll = async () => {
    try {
      await api.clearCalibrationEntries(id);
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
      onToast(`Calibration saved: rms=${r.Rms.toFixed(3)}px ${ok ? '(good)' : '(high - capture more varied snapshots)'}`, ok ? 'success' : 'error');
    } catch {
      onToast('Calibration failed - need at least 4 snapshots', 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="max-w-4xl mx-auto space-y-6">
      <div className="flex items-center gap-3">
        <button onClick={() => navigate('/calibration')} className="p-2 rounded-lg bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700">
          <ArrowLeft className="w-5 h-5" />
        </button>
        <div>
          <h1 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Camera className="w-5 h-5" />Camera Calibration</h1>
          <p className="text-sm text-gray-500 dark:text-gray-400">Session #{id}</p>
        </div>
      </div>

      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
        <h2 className="font-semibold text-gray-900 dark:text-white mb-3">1. Capture snapshots</h2>
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
          <MjpegStream sinkId={state.session.PreviewSinkId} onStop={() => {}} onError={() => onToast('Preview error', 'error')} />
          <CoverageHeatmap coverage={coverage} />
        </div>
        <div className="flex items-center gap-2 mt-4">
          <button onClick={capture} className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded text-sm font-medium">Capture snapshot</button>
          <button onClick={clearAll} className="px-3 py-2 bg-red-100 dark:bg-red-900 hover:bg-red-200 text-red-700 dark:text-red-200 rounded text-sm font-medium flex items-center gap-1"><Trash2 className="w-4 h-4" />Clear all</button>
          <span className="text-sm text-gray-600 dark:text-gray-400 ml-auto">{snapshotCount} snapshot{snapshotCount === 1 ? '' : 's'} saved (need 4+)</span>
        </div>
      </div>

      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
        <h2 className="font-semibold text-gray-900 dark:text-white mb-3">2. Run &amp; save</h2>
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
        <div className="flex items-center gap-4 mt-4">
          <button onClick={refresh} className="text-xs text-blue-600 hover:text-blue-700 flex items-center gap-1"><RefreshCw className="w-3 h-3" />Refresh</button>
          <button onClick={() => navigate('/calibration')} className="text-xs text-gray-600 dark:text-gray-400 hover:underline">Finish</button>
        </div>
      </div>
    </div>
  );
};
