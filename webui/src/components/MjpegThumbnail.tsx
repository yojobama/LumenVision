import React, { useEffect, useRef } from 'react';
import { ApiService } from '../services/ApiService';

const api = new ApiService();
const POLL_MS = 500;

// A small live view for the MJPEG fallback: fetches the sink's latest frame twice a second and draws it, so it never holds a connection open
// (MjpegStream does, and a handful of those starve the page's other requests).
export const MjpegThumbnail: React.FC<{ sinkId: number; className?: string }> = ({ sinkId, className = '' }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const controller = new AbortController();
    let timer: ReturnType<typeof setTimeout> | undefined;

    const poll = async () => {
      try {
        const response = await fetch(api.getMjpegFrameUrl(sinkId), { signal: controller.signal, cache: 'no-store' });
        if (response.status === 200) {
          const bitmap = await createImageBitmap(await response.blob());
          const canvas = canvasRef.current;
          if (canvas) {
            if (canvas.width !== bitmap.width || canvas.height !== bitmap.height) {
              canvas.width = bitmap.width;
              canvas.height = bitmap.height;
            }
            canvas.getContext('2d')?.drawImage(bitmap, 0, 0);
          }
          bitmap.close();
        }
      } catch {
        // aborted on unmount, or the server is restarting: try again on the next tick
      }
      if (!controller.signal.aborted) timer = setTimeout(poll, POLL_MS);
    };

    poll();
    return () => {
      controller.abort();
      if (timer) clearTimeout(timer);
    };
  }, [sinkId]);

  return (
    <div className={`bg-black rounded-lg overflow-hidden h-full ${className}`}>
      <canvas ref={canvasRef} className="w-full h-full object-contain" />
    </div>
  );
};
