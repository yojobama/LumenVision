import React, { useEffect, useState, useCallback } from 'react';
import { useParams, useNavigate, Navigate } from 'react-router-dom';
import { ArrowLeft, Layers, CheckCircle, XCircle, Trash2, Play, RefreshCw } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import { CoverageHeatmap } from '../components/CoverageHeatmap';
import { MjpegStream } from '../components/MjpegStream';
import { useCalibrationSession } from '../hooks/useCalibrationSession';
import type { StereoCalibrationResult, CalibrationCoverage } from '../types';
import { EPIPOLAR_RMS_GATE } from '../types';

const api = new ApiService();

// Stereo calibration wizard (capture, run, result) for a session started from the Calibration tab.
// EpipolarRms is the real quality gate.
export const StereoCalibrationWizardPage: React.FC<{
  onToast: (m: string, t: 'success' | 'error' | 'info') => void;
}> = ({ onToast }) => {
  const { sessionId } = useParams<{ sessionId: string }>();
  const navigate = useNavigate();
  const id = Number(sessionId);
  const state = useCalibrationSession(id, 'stereo');

  const [pairCount, setPairCount] = useState(0);
  const [coverageLeft, setCoverageLeft] = useState<CalibrationCoverage | null>(null);
  const [coverageRight, setCoverageRight] = useState<CalibrationCoverage | null>(null);
  const [result, setResult] = useState<StereoCalibrationResult | null>(null);
  const [busy, setBusy] = useState(false);
  const ready = state.status === 'ready';

  const refresh = useCallback(async () => {
    try {
      const [count, covL, covR] = await Promise.all([
        api.getCalibrationCount(id),
        api.getCalibrationCoverage(id, 'left'),
        api.getCalibrationCoverage(id, 'right'),
      ]);
      setPairCount(count);
      setCoverageLeft(covL);
      setCoverageRight(covR);
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
      if (!saved) { onToast('No matched checkerboard pair yet - check both eyes see the board', 'error'); return; }
      await refresh();
      onToast('Pair saved', 'success');
    } catch {
      onToast('Failed to save pair', 'error');
    }
  };

  const clearAll = async () => {
    try {
      await api.clearCalibrationEntries(id);
      await refresh();
      onToast('Pairs cleared', 'info');
    } catch {
      onToast('Failed to clear pairs', 'error');
    }
  };

  const run = async () => {
    setBusy(true);
    try {
      const r = await api.runStereoCalibration(id);
      setResult(r);
      const ok = r.EpipolarRms < EPIPOLAR_RMS_GATE;
      onToast(`Calibration saved: epipolarRms=${r.EpipolarRms.toFixed(3)}px ${ok ? '(good)' : '(too high - recapture more pairs)'}`, ok ? 'success' : 'error');
    } catch {
      onToast('Calibration failed - need at least 8 pairs', 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="max-w-5xl mx-auto space-y-6">
      <div className="flex items-center gap-3">
        <button onClick={() => navigate('/calibration')} className="p-2 rounded-lg bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700">
          <ArrowLeft className="w-5 h-5" />
        </button>
        <div>
          <h1 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Layers className="w-5 h-5" />Stereo Calibration</h1>
          <p className="text-sm text-gray-500 dark:text-gray-400">Session #{id}</p>
        </div>
      </div>

      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
        <h2 className="font-semibold text-gray-900 dark:text-white mb-3">1. Capture pairs</h2>
        <p className="text-xs text-gray-500 dark:text-gray-400 mb-3">Left eye on the left, right eye on the right; each shows its own detection state.</p>
        <MjpegStream sinkId={state.session.PreviewSinkId} onStop={() => {}} onError={() => onToast('Preview error', 'error')} />
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mt-4">
          <div>
            <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">Left coverage</p>
            <CoverageHeatmap coverage={coverageLeft} />
          </div>
          <div>
            <p className="text-xs text-gray-500 dark:text-gray-400 mb-1">Right coverage</p>
            <CoverageHeatmap coverage={coverageRight} />
          </div>
        </div>
        <div className="flex items-center gap-2 mt-4">
          <button onClick={capture} className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded text-sm font-medium">Capture pair</button>
          <button onClick={clearAll} className="px-3 py-2 bg-red-100 dark:bg-red-900 hover:bg-red-200 text-red-700 dark:text-red-200 rounded text-sm font-medium flex items-center gap-1"><Trash2 className="w-4 h-4" />Clear all</button>
          <span className="text-sm text-gray-600 dark:text-gray-400 ml-auto">{pairCount} pair{pairCount === 1 ? '' : 's'} saved (need 8+)</span>
        </div>
      </div>

      <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
        <h2 className="font-semibold text-gray-900 dark:text-white mb-3">2. Run &amp; save</h2>
        <p className="text-xs text-gray-500 dark:text-gray-400 mb-3">
          Check <strong>epipolarRms</strong> - it must be under {EPIPOLAR_RMS_GATE}px for a stereo depth node to
          produce useful output; stereoRms alone doesn't predict this.
        </p>
        <button disabled={busy || pairCount < 8} onClick={run} className="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded text-sm font-medium flex items-center gap-2">
          <Play className="w-4 h-4" />Run calibration
        </button>
        {result && (
          <div className="mt-4 flex items-center gap-2">
            {result.EpipolarRms < EPIPOLAR_RMS_GATE ? <CheckCircle className="w-5 h-5 text-green-500" /> : <XCircle className="w-5 h-5 text-red-500" />}
            <span className="text-sm text-gray-700 dark:text-gray-300">
              epipolarRms={result.EpipolarRms.toFixed(3)}px · stereoRms={result.StereoRms.toFixed(3)} · baseline={result.BaselineMeters.toFixed(3)}m
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
