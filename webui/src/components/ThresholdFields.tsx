import React from 'react';

// Confidence and NMS cutoffs of an object detector. Confidence is how sure the network must be to report a box; NMS is how much two boxes may
// overlap before the weaker one is dropped.
export const ThresholdFields: React.FC<{
  confThreshold: number;
  nmsThreshold: number;
  onChange: (values: { confThreshold: number; nmsThreshold: number }) => void;
  disabled?: boolean;
}> = ({ confThreshold, nmsThreshold, onChange, disabled }) => (
  <div className="space-y-2">
    <label className="block text-xs text-gray-500 dark:text-gray-400"
      title="How sure the network must be before a box is reported. Raise it to cut false detections, lower it to catch faint ones.">
      <span className="flex justify-between"><span>Confidence cutoff</span><span>{confThreshold.toFixed(2)}</span></span>
      <input type="range" min={0.01} max={1} step={0.01} value={confThreshold} disabled={disabled} className="w-full"
        onChange={e => onChange({ confThreshold: parseFloat(e.target.value), nmsThreshold })} />
    </label>
    <label className="block text-xs text-gray-500 dark:text-gray-400"
      title="How much two boxes may overlap before the weaker one is dropped. Lower removes more duplicates but can merge neighbouring objects.">
      <span className="flex justify-between"><span>Overlap cutoff (NMS)</span><span>{nmsThreshold.toFixed(2)}</span></span>
      <input type="range" min={0.01} max={1} step={0.01} value={nmsThreshold} disabled={disabled} className="w-full"
        onChange={e => onChange({ confThreshold, nmsThreshold: parseFloat(e.target.value) })} />
    </label>
  </div>
);
