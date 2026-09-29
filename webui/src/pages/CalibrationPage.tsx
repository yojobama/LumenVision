import React, { useCallback, useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Camera, Layers, Play, Square, CheckCircle, AlertTriangle, XCircle } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import type {
  Source, CalibrationBoard, CalibrationSession, CalibrationStatus, StoredCameraCalibration, StoredStereoCalibration,
} from '../types';
import { EPIPOLAR_RMS_GATE } from '../types';

const api = new ApiService();

type Toast = (m: string, t: 'success' | 'error' | 'info') => void;

const BOARD_CHECKERBOARD = 0;
const BOARD_CHARUCO = 1;
const CHECKERBOARD_DEFAULTS: CalibrationBoard = { boardType: BOARD_CHECKERBOARD, rows: 6, cols: 9, squareSizeMeters: 0.025 };
const CHARUCO_DEFAULTS: CalibrationBoard = { boardType: BOARD_CHARUCO, rows: 5, cols: 7, squareSizeMeters: 0.04, markerSizeMeters: 0.03 };

const inputClass = 'mt-1 w-full rounded border-gray-300 dark:border-gray-600 dark:bg-gray-700 dark:text-white';
const cardClass = 'bg-white dark:bg-gray-800 rounded-lg shadow p-6';

const cameraSources = (sources: Source[]) => sources.filter(s => s.type === 'camera');

const CameraSelect: React.FC<{
  sources: Source[]; value: number | ''; onChange: (id: number | '') => void; label: string;
}> = ({ sources, value, onChange, label }) => (
  <label className="block text-sm">
    <span className="text-gray-600 dark:text-gray-400">{label}</span>
    <select className={inputClass} value={value} onChange={e => onChange(e.target.value ? Number(e.target.value) : '')}>
      <option value="">Select a camera…</option>
      {cameraSources(sources).map(s => <option key={s.id} value={s.id}>{s.name} (#{s.id})</option>)}
    </select>
  </label>
);

const NumberField: React.FC<{
  label: string; value: number; step?: string; onChange: (v: number) => void;
}> = ({ label, value, step, onChange }) => (
  <label className="block text-sm">
    <span className="text-gray-600 dark:text-gray-400">{label}</span>
    <input type="number" step={step} className={inputClass} value={value} onChange={e => onChange(Number(e.target.value))} />
  </label>
);

const BoardFields: React.FC<{
  board: CalibrationBoard; onChange: (b: CalibrationBoard) => void; allowCharuco: boolean;
}> = ({ board, onChange, allowCharuco }) => (
  <div className="grid grid-cols-2 md:grid-cols-4 gap-2">
    {allowCharuco && (
      <label className="block text-sm col-span-2 md:col-span-4">
        <span className="text-gray-600 dark:text-gray-400">Board</span>
        <select className={inputClass} value={board.boardType}
          onChange={e => onChange(Number(e.target.value) === BOARD_CHARUCO ? CHARUCO_DEFAULTS : CHECKERBOARD_DEFAULTS)}>
          <option value={BOARD_CHECKERBOARD}>Checkerboard (interior corners)</option>
          <option value={BOARD_CHARUCO}>ChArUco (squares)</option>
        </select>
      </label>
    )}
    <NumberField label="Rows" value={board.rows} onChange={rows => onChange({ ...board, rows })} />
    <NumberField label="Cols" value={board.cols} onChange={cols => onChange({ ...board, cols })} />
    <NumberField label="Square (m)" step="0.001" value={board.squareSizeMeters} onChange={squareSizeMeters => onChange({ ...board, squareSizeMeters })} />
    {board.boardType === BOARD_CHARUCO && (
      <NumberField label="Marker (m)" step="0.001" value={board.markerSizeMeters ?? 0.03} onChange={markerSizeMeters => onChange({ ...board, markerSizeMeters })} />
    )}
  </div>
);

const CameraCalibrationCard: React.FC<{ sources: Source[]; onToast: Toast }> = ({ sources, onToast }) => {
  const navigate = useNavigate();
  const [camera, setCamera] = useState<number | ''>('');
  const [board, setBoard] = useState<CalibrationBoard>(CHECKERBOARD_DEFAULTS);
  const [status, setStatus] = useState<CalibrationStatus | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    setStatus(null);
    if (camera === '') return;
    let cancelled = false;
    api.getCalibrationStatus(camera).then(s => { if (!cancelled) setStatus(s); }).catch(() => {});
    return () => { cancelled = true; };
  }, [camera]);

  const start = async () => {
    if (camera === '') { onToast('Select a camera first', 'error'); return; }
    setBusy(true);
    try {
      const session = await api.startCameraCalibration(camera, board);
      navigate(`/calibration/camera/${session.SessionId}`);
    } catch (e) {
      onToast(`Failed to start calibration: ${e}`, 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className={cardClass}>
      <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-1 flex items-center gap-2"><Camera className="w-5 h-5" />Camera calibration</h2>
      <p className="text-sm text-gray-500 dark:text-gray-400 mb-4">
        Capture views of a printed board to compute a camera's intrinsics. The result is saved per camera and resolution
        and used automatically by AprilTag pipelines.
      </p>
      <div className="space-y-3 mb-4">
        <CameraSelect sources={sources} value={camera} onChange={setCamera} label="Camera" />
        {status && (
          <p className="text-xs">
            {!status.HasCalibration && <span className="text-gray-500 dark:text-gray-400">Not calibrated yet</span>}
            {status.HasCalibration && status.MatchesCurrentResolution && (
              <span className="text-green-600 dark:text-green-400 flex items-center gap-1"><CheckCircle className="w-3 h-3" />Calibrated at {status.CalibratedWidth}×{status.CalibratedHeight}</span>
            )}
            {status.HasCalibration && !status.MatchesCurrentResolution && (
              <span className="text-amber-600 dark:text-amber-400 flex items-center gap-1"><AlertTriangle className="w-3 h-3" />Calibrated at {status.CalibratedWidth}×{status.CalibratedHeight}, not this camera's current resolution</span>
            )}
          </p>
        )}
        <BoardFields board={board} onChange={setBoard} allowCharuco />
      </div>
      <button disabled={busy} onClick={start} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium flex items-center gap-2">
        <Play className="w-4 h-4" />Start calibration
      </button>
    </div>
  );
};

const StereoCalibrationCard: React.FC<{ sources: Source[]; onToast: Toast }> = ({ sources, onToast }) => {
  const navigate = useNavigate();
  const [mode, setMode] = useState<'two' | 'one'>('two');
  const [left, setLeft] = useState<number | ''>('');
  const [right, setRight] = useState<number | ''>('');
  const [single, setSingle] = useState<number | ''>('');
  const [board, setBoard] = useState<CalibrationBoard>(CHECKERBOARD_DEFAULTS);
  const [busy, setBusy] = useState(false);

  const start = async () => {
    setBusy(true);
    try {
      let session: CalibrationSession;
      if (mode === 'two') {
        if (left === '' || right === '') { onToast('Select both cameras first', 'error'); return; }
        session = await api.startStereoCalibration(left, right, board);
      } else {
        if (single === '') { onToast('Select a camera first', 'error'); return; }
        session = await api.startStereoSplitCalibration(single, board);
      }
      navigate(`/calibration/stereo/${session.SessionId}`);
    } catch (e) {
      onToast(`Failed to start calibration: ${e}`, 'error');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className={cardClass}>
      <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-1 flex items-center gap-2"><Layers className="w-5 h-5" />Stereo calibration</h2>
      <p className="text-sm text-gray-500 dark:text-gray-400 mb-4">
        Capture checkerboard pairs from two cameras (or one side-by-side stereo camera) to compute the stereo geometry used by
        stereo depth.
      </p>
      <div className="flex gap-4 mb-3 text-sm text-gray-700 dark:text-gray-300">
        <label className="flex items-center gap-2"><input type="radio" checked={mode === 'two'} onChange={() => setMode('two')} />Two cameras</label>
        <label className="flex items-center gap-2"><input type="radio" checked={mode === 'one'} onChange={() => setMode('one')} />One side-by-side camera</label>
      </div>
      <div className="space-y-3 mb-4">
        {mode === 'two' ? (
          <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
            <CameraSelect sources={sources} value={left} onChange={setLeft} label="Left camera" />
            <CameraSelect sources={sources} value={right} onChange={setRight} label="Right camera" />
          </div>
        ) : (
          <CameraSelect sources={sources} value={single} onChange={setSingle} label="Camera (frame split in half into left/right eyes)" />
        )}
        <BoardFields board={board} onChange={setBoard} allowCharuco={false} />
      </div>
      <button disabled={busy} onClick={start} className="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm font-medium flex items-center gap-2">
        <Play className="w-4 h-4" />Start calibration
      </button>
    </div>
  );
};

const RunningSessions: React.FC<{ onToast: Toast }> = ({ onToast }) => {
  const navigate = useNavigate();
  const [sessions, setSessions] = useState<CalibrationSession[]>([]);

  const load = useCallback(() => {
    api.getCalibrationSessions().then(setSessions).catch(() => setSessions([]));
  }, []);

  useEffect(() => { load(); }, [load]);

  if (sessions.length === 0) return null;

  const stop = async (id: number) => {
    try {
      await api.stopCalibrationSession(id);
      load();
    } catch {
      onToast('Failed to stop session', 'error');
    }
  };

  return (
    <div className={cardClass}>
      <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-3">Running sessions</h2>
      <div className="space-y-2">
        {sessions.map(s => (
          <div key={s.SessionId} className="flex items-center justify-between border border-gray-200 dark:border-gray-700 rounded p-3">
            <span className="text-sm text-gray-900 dark:text-white capitalize">{s.Kind} session #{s.SessionId}</span>
            <div className="flex gap-2">
              <button onClick={() => navigate(`/calibration/${s.Kind}/${s.SessionId}`)} className="px-3 py-1 bg-purple-600 hover:bg-purple-700 text-white rounded text-xs font-medium">Resume</button>
              <button onClick={() => stop(s.SessionId)} className="px-3 py-1 bg-gray-200 dark:bg-gray-700 hover:bg-gray-300 dark:hover:bg-gray-600 rounded text-xs font-medium flex items-center gap-1"><Square className="w-3 h-3" />Stop</button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

const SavedCalibrations: React.FC = () => {
  const [camera, setCamera] = useState<StoredCameraCalibration[] | null>(null);
  const [stereo, setStereo] = useState<StoredStereoCalibration[] | null>(null);

  useEffect(() => {
    api.getSavedCalibrations().then(setCamera).catch(() => setCamera([]));
    api.getSavedStereoCalibrations().then(setStereo).catch(() => setStereo([]));
  }, []);

  const date = (ms: number) => new Date(ms).toLocaleDateString();

  return (
    <div className={cardClass}>
      <h2 className="text-lg font-semibold text-gray-900 dark:text-white mb-3">Saved calibrations</h2>
      <h3 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Cameras</h3>
      {camera === null && <p className="text-sm text-gray-500 dark:text-gray-400">Loading…</p>}
      {camera?.length === 0 && <p className="text-sm text-gray-500 dark:text-gray-400">No saved camera calibrations yet</p>}
      <ul className="space-y-1 mb-4">
        {camera?.map((c, i) => (
          <li key={`${c.CameraPath}-${c.CalibratedAtUnixMs}-${i}`} className="text-sm text-gray-700 dark:text-gray-300">
            <span className="font-mono text-xs">{c.CameraPath}</span> · {c.Result.ImageWidth}×{c.Result.ImageHeight} · rms {c.Result.Rms.toFixed(3)} · {date(c.CalibratedAtUnixMs)}
          </li>
        ))}
      </ul>
      <h3 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Stereo pairs</h3>
      {stereo === null && <p className="text-sm text-gray-500 dark:text-gray-400">Loading…</p>}
      {stereo?.length === 0 && <p className="text-sm text-gray-500 dark:text-gray-400">No saved stereo calibrations yet</p>}
      <ul className="space-y-1">
        {stereo?.map((c, i) => (
          <li key={`${c.LeftCameraPath}-${c.RightCameraPath}-${c.CalibratedAtUnixMs}-${i}`} className="text-sm text-gray-700 dark:text-gray-300 flex items-center gap-2">
            {c.Result.EpipolarRms < EPIPOLAR_RMS_GATE ? <CheckCircle className="w-3 h-3 text-green-500" /> : <XCircle className="w-3 h-3 text-red-500" />}
            <span>
              <span className="font-mono text-xs">{c.LeftCameraPath}{c.RightCameraPath !== c.LeftCameraPath ? ` + ${c.RightCameraPath}` : ' (split)'}</span> ·
              {' '}{c.Result.ImageWidth}×{c.Result.ImageHeight} · epipolar {c.Result.EpipolarRms.toFixed(3)}px · baseline {c.Result.BaselineMeters.toFixed(3)}m · {date(c.CalibratedAtUnixMs)}
            </span>
          </li>
        ))}
      </ul>
    </div>
  );
};

// Top-level Calibration tab: start camera/stereo calibration sessions and review saved results.
export const CalibrationPage: React.FC<{ sources: Source[]; onToast: Toast }> = ({ sources, onToast }) => (
  <div className="max-w-4xl mx-auto space-y-6">
    <RunningSessions onToast={onToast} />
    <CameraCalibrationCard sources={sources} onToast={onToast} />
    <StereoCalibrationCard sources={sources} onToast={onToast} />
    <SavedCalibrations />
  </div>
);
