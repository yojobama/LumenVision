import React, { useState } from 'react';
import { ApiService } from '../services/ApiService';
import type { WsSink } from '../types';

const api = new ApiService();

const field = 'w-full px-2 py-1 text-xs border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white';

// Bitrate, frame rate, size and JPEG quality of a camera's preview streams. A change applies to the running stream (the WebRTC encoder
// restarts on a keyframe) and is remembered by the server.
export const StreamSettingsPanel: React.FC<{
  webrtc?: WsSink;
  mjpeg?: WsSink;
  onToast: (message: string, type: 'success' | 'error' | 'info') => void;
}> = ({ webrtc, mjpeg, onToast }) => {
  const [bitrate, setBitrate] = useState(webrtc?.StreamBitrateKbps ?? 4000);
  const [fps, setFps] = useState(webrtc?.StreamFps ?? 30);
  const [webrtcScale, setWebrtcScale] = useState(webrtc?.StreamScaleDivisor ?? 1);
  const [quality, setQuality] = useState(mjpeg?.StreamJpegQuality ?? 80);
  const [mjpegScale, setMjpegScale] = useState(mjpeg?.StreamScaleDivisor ?? 1);

  const run = async (action: () => Promise<void>) => {
    try {
      await action();
      onToast('Stream settings applied', 'success');
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to change the stream settings', 'error');
    }
  };

  const scaleSelect = (value: number, set: (n: number) => void) => (
    <select value={value} onChange={e => set(parseInt(e.target.value))} className={field}>
      {[1, 2, 3, 4, 6, 8].map(n => <option key={n} value={n}>{n === 1 ? 'Full size' : `1/${n} width and height`}</option>)}
    </select>
  );

  return (
    <div className="space-y-3 text-xs">
      {webrtc && (
        <div className="space-y-2">
          <div className="font-medium text-gray-500 dark:text-gray-400">Live view (WebRTC)</div>
          <label className="block text-gray-500 dark:text-gray-400">Bitrate (kbps)
            <input type="number" min={100} max={50000} step={100} value={bitrate} onChange={e => setBitrate(parseInt(e.target.value) || 4000)} className={field} />
          </label>
          <label className="block text-gray-500 dark:text-gray-400">Frame rate
            <input type="number" min={1} max={120} value={fps} onChange={e => setFps(parseInt(e.target.value) || 30)} className={field} />
          </label>
          <label className="block text-gray-500 dark:text-gray-400">Size {scaleSelect(webrtcScale, setWebrtcScale)}</label>
          <button onClick={() => run(() => api.setWebRTCSettings(webrtc.Id, bitrate, fps, webrtcScale))}
            className="w-full px-2 py-1 bg-blue-600 text-white rounded hover:bg-blue-700">Apply</button>
        </div>
      )}
      {mjpeg && (
        <div className="space-y-2">
          <div className="font-medium text-gray-500 dark:text-gray-400">Snapshot stream (MJPEG)</div>
          <label className="block text-gray-500 dark:text-gray-400">JPEG quality ({quality})
            <input type="range" min={1} max={100} value={quality} onChange={e => setQuality(parseInt(e.target.value))} className="w-full" />
          </label>
          <label className="block text-gray-500 dark:text-gray-400">Size {scaleSelect(mjpegScale, setMjpegScale)}</label>
          <button onClick={() => run(() => api.setMjpegSettings(mjpeg.Id, quality, mjpegScale))}
            className="w-full px-2 py-1 bg-blue-600 text-white rounded hover:bg-blue-700">Apply</button>
        </div>
      )}
    </div>
  );
};
