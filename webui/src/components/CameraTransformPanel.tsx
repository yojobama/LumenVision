import React, { useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import type { FrameTransform } from '../types';
import { ToggleSwitch } from './ToggleSwitch';

const api = new ApiService();

const IDENTITY: FrameTransform = { Rotation: 0, FlipHorizontal: false, FlipVertical: false, CropX: 0, CropY: 0, CropWidth: 0, CropHeight: 0 };

// Crop, rotate and mirror a camera's frames (for a camera mounted sideways or upside down, or to look at part of the image). Saved
// calibrations follow the transform on their own; calibrate with it cleared.
export const CameraTransformPanel: React.FC<{
  sourceId: number;
  onToast: (message: string, type: 'success' | 'error' | 'info') => void;
}> = ({ sourceId, onToast }) => {
  const [transform, setTransform] = useState<FrameTransform>(IDENTITY);

  useEffect(() => {
    let cancelled = false;
    api.getCameraTransform(sourceId)
      .then(t => { if (!cancelled) setTransform(t); })
      .catch(() => { if (!cancelled) setTransform(IDENTITY); });
    return () => { cancelled = true; };
  }, [sourceId]);

  const apply = async (next: FrameTransform) => {
    setTransform(next);
    try {
      await api.setCameraTransform(sourceId, next);
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to set the frame transform', 'error');
    }
  };

  const numberField = (label: string, key: 'CropX' | 'CropY' | 'CropWidth' | 'CropHeight') => (
    <label className="text-xs text-gray-500 dark:text-gray-400">
      {label}
      <input type="number" min={0} value={transform[key]} onChange={e => setTransform({ ...transform, [key]: Math.max(0, parseInt(e.target.value) || 0) })}
        className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white" />
    </label>
  );

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Rotation (clockwise)</label>
        <select value={transform.Rotation} onChange={e => apply({ ...transform, Rotation: parseInt(e.target.value) })}
          className="w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white">
          {[0, 90, 180, 270].map(degrees => <option key={degrees} value={degrees}>{degrees} degrees</option>)}
        </select>
      </div>
      <div className="flex items-center justify-between">
        <span className="text-xs text-gray-700 dark:text-gray-300">Mirror horizontally</span>
        <ToggleSwitch enabled={transform.FlipHorizontal} onChange={enabled => apply({ ...transform, FlipHorizontal: enabled })} />
      </div>
      <div className="flex items-center justify-between">
        <span className="text-xs text-gray-700 dark:text-gray-300">Mirror vertically</span>
        <ToggleSwitch enabled={transform.FlipVertical} onChange={enabled => apply({ ...transform, FlipVertical: enabled })} />
      </div>
      <div>
        <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">Crop in camera pixels (width and height 0 = none)</div>
        <div className="grid grid-cols-4 gap-2">
          {numberField('X', 'CropX')}
          {numberField('Y', 'CropY')}
          {numberField('Width', 'CropWidth')}
          {numberField('Height', 'CropHeight')}
        </div>
        <div className="flex gap-2 mt-2">
          <button onClick={() => apply(transform)} className="flex-1 px-2 py-1 bg-blue-600 text-white rounded text-xs hover:bg-blue-700">Apply crop</button>
          <button onClick={() => apply(IDENTITY)} className="px-2 py-1 bg-gray-200 dark:bg-gray-700 rounded text-xs">Reset all</button>
        </div>
      </div>
    </div>
  );
};
