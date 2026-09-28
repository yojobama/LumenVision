import React, { useEffect, useState, useCallback } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { ArrowLeft, Layers, CheckCircle, XCircle, Trash2, Play, RefreshCw, Camera, Columns2 } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import { CoverageHeatmap } from '../components/CoverageHeatmap';
import type { StereoCalibrationResult, CalibrationCoverage, Source } from '../types';
import { EPIPOLAR_RMS_GATE } from '../types';

const api = new ApiService();

// Stereo calibration wizard (bind, capture, run, result). epipolarRms is the real quality gate.
export const StereoCalibrationWizardPage: React.FC<{
  sources: Source[];
  onToast: (m: string, t: 'success' | 'error' | 'info') => void;
}> = ({ sources, onToast }) => {
  const { sinkId } = useParams<{ sinkId: string }>();
  const navigate = useNavigate();
  const id = Number(sinkId);

  // one camera or two: a side-by-side/top-bottom stereo camera is split into two RoiSource nodes;
  // 'choose' is the first step and 'two'/'one' select the flow
  const [mode, setMode] = useState<'choose' | 'two' | 'one'>('choose');
  const [singleCameraId, setSingleCameraId] = useState<number | ''>('');
  const [splitting, setSplitting] = useState(false);
  const cameraSources = sources.filter(s => s.type === 'camera');

  const [left, setLeft] = useState<number | ''>('');
  const [right, setRight] = useState<number | ''>('');
  const [bound, setBound] = useState(false);
  const [pairCount, setPairCount] = useState(0);
  const [coverageLeft, setCoverageLeft] = useState<CalibrationCoverage | null>(null);
  const [coverageRight, setCoverageRight] = useState<CalibrationCoverage | null>(null);
  const [result, setResult] = useState<StereoCalibrationResult | null>(null);
  const [busy, setBusy] = useState(false);

  const refresh = useCallback(async () => {
    try {
      const [count, covL, covR] = await Promise.all([
        api.getStereoCalibrationPairCount(id),
        api.getStereoCalibrationCoverage(id, 'left'),
        api.getStereoCalibrationCoverage(id, 'right'),
      ]);
      setPairCount(count);
      setCoverageLeft(covL);
      setCoverageRight(covR);
    } catch {
      onToast('Failed to refresh calibration state', 'error');
    }
  }, [id]);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, 2000);
    return () => clearInterval(interval);
  }, [refresh]);

  // Splits the chosen camera's frame into left/right eyes via two RoiSource nodes and binds them,
  // skipping the manual bind step.
  const splitAndBind = async () => {
    if (!singleCameraId) return;
    setSplitting(true);
    try {
      const currentMode = await api.getCameraCurrentMode(singleCameraId as number);
      const leftWidth = Math.floor(currentMode.Width / 2);
      const rightWidth = currentMode.Width - leftWidth;
      const leftId = await api.createRoiSource(singleCameraId as number, 0, 0, leftWidth, currentMode.Height);
      const rightId = await api.createRoiSource(singleCameraId as number, leftWidth, 0, rightWidth, currentMode.Height);
      await api.bindStereoSources(id, leftId, rightId);
      setLeft(leftId);
      setRight(rightId);
      setBound(true);
      onToast('Camera split into left/right eyes and bound', 'success');
    } catch {
      onToast('Failed to split camera into left/right eyes', 'error');
    } finally {
      setSplitting(false);
    }
  };

  const bind = async () => {
    if (!left || !right) return;
    setBusy(true);
    try {
      await api.bindStereoSources(id, left as number, right as number);
      setBound(true);
      onToast('Cameras bound', 'success');
    } catch {
      onToast('Failed to bind cameras', 'error');
    } finally {
      setBusy(false);
    }
  };

  const capture = async () => {
    try {
      const saved = await api.saveStereoCalibrationDetection(id);
      if (!saved) { onToast('No matched checkerboard pair yet - check both cameras see the board', 'error'); return; }
      await refresh();
      onToast(`Pair saved (${pairCount + 1} total)`, 'success');
    } catch {
      onToast('Failed to save pair', 'error');
    }
  };

  const clearAll = async () => {
    try {
      await api.clearStereoCalibrationPairs(id);
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
      onToast(`Calibration done: epipolarRms=${r.EpipolarRms.toFixed(3)}px ${ok ? '(good)' : '(too high - recapture more pairs)'}`, ok ? 'success' : 'error');
    } catch {
      onToast('Calibration failed - need at least 8 pairs', 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="fixed inset-0 bg-gray-50 dark:bg-gray-900 z-40 overflow-y-auto">
      <div className="max-w-5xl mx-auto p-6 space-y-6">
        <div className="flex items-center gap-3">
          <button onClick={() => navigate('/stereo')} className="p-2 rounded-lg bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700">
            <ArrowLeft className="w-5 h-5" />
          </button>
          <div>
            <h1 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Layers className="w-5 h-5" />Stereo Calibration Wizard</h1>
            <p className="text-sm text-gray-500 dark:text-gray-400">Sink #{id}</p>
          </div>
        </div>

        {mode === 'choose' && (
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
            <h2 className="font-semibold text-gray-900 dark:text-white mb-3">1. One camera or two?</h2>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <button
                onClick={() => setMode('two')}
                className="flex flex-col items-center gap-2 p-6 border-2 border-gray-200 dark:border-gray-700 rounded-lg hover:border-blue-400 dark:hover:border-blue-500 text-center"
              >
                <Layers className="w-8 h-8 text-blue-600" />
                <span className="font-medium text-gray-900 dark:text-white">Two separate cameras</span>
                <span className="text-xs text-gray-500 dark:text-gray-400">Bind two already-created camera sources as left/right eyes</span>
              </button>
              <button
                onClick={() => setMode('one')}
                className="flex flex-col items-center gap-2 p-6 border-2 border-gray-200 dark:border-gray-700 rounded-lg hover:border-blue-400 dark:hover:border-blue-500 text-center"
              >
                <Columns2 className="w-8 h-8 text-blue-600" />
                <span className="font-medium text-gray-900 dark:text-white">One side-by-side stereo camera</span>
                <span className="text-xs text-gray-500 dark:text-gray-400">Splits a single camera's frame in half into two independent eyes (RoiSource)</span>
              </button>
            </div>
          </div>
        )}

        {mode === 'one' && !bound && (
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
            <h2 className="font-semibold text-gray-900 dark:text-white mb-3">1. Split &amp; bind</h2>
            <div className="grid grid-cols-1 md:grid-cols-3 gap-3 items-end">
              <label className="block text-sm md:col-span-2">
                <span className="text-gray-600 dark:text-gray-400">Camera</span>
                <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={singleCameraId} onChange={e => setSingleCameraId(e.target.value ? Number(e.target.value) : '')}>
                  <option value="">Select…</option>
                  {cameraSources.map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
                </select>
              </label>
              <button disabled={splitting || !singleCameraId} onClick={splitAndBind} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium flex items-center justify-center gap-2">
                <Camera className="w-4 h-4" />{splitting ? 'Splitting…' : 'Split & bind'}
              </button>
            </div>
          </div>
        )}

        {mode === 'two' && (
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
            <h2 className="font-semibold text-gray-900 dark:text-white mb-3">2. Bind cameras</h2>
            <div className="grid grid-cols-1 md:grid-cols-3 gap-3 items-end">
              <label className="block text-sm">
                <span className="text-gray-600 dark:text-gray-400">Left camera</span>
                <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={left} onChange={e => setLeft(e.target.value ? Number(e.target.value) : '')}>
                  <option value="">Select…</option>
                  {sources.map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
                </select>
              </label>
              <label className="block text-sm">
                <span className="text-gray-600 dark:text-gray-400">Right camera</span>
                <select className="mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white" value={right} onChange={e => setRight(e.target.value ? Number(e.target.value) : '')}>
                  <option value="">Select…</option>
                  {sources.map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
                </select>
              </label>
              <button disabled={busy || !left || !right} onClick={bind} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium">Bind</button>
            </div>
            {bound && <p className="text-xs text-green-600 dark:text-green-400 mt-2">Bound</p>}
          </div>
        )}

        {bound && (
        <>
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 className="font-semibold text-gray-900 dark:text-white mb-3">3. Capture pairs</h2>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
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
          <h2 className="font-semibold text-gray-900 dark:text-white mb-3">4. Run &amp; result</h2>
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
          <button onClick={refresh} className="mt-4 text-xs text-blue-600 hover:text-blue-700 flex items-center gap-1"><RefreshCw className="w-3 h-3" />Refresh</button>
        </div>
        </>
        )}
      </div>
    </div>
  );
};
