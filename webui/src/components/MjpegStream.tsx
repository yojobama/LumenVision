import React, { useEffect, useRef, useState } from 'react';
import { RefreshCw, Square, Video, Activity, XCircle } from 'lucide-react';
import { WebRTCStreamProps } from '../types';
import { ApiService } from '../services/ApiService';

const BOUNDARY = '--lumenvision-mjpeg-frame';
const HEADER_TERMINATOR = '\r\n\r\n';
const encoder = new TextEncoder();
const decoder = new TextDecoder();

// Finds the next occurrence of an ASCII needle in a byte buffer (the stream framing is ASCII
// apart from the JPEG payload).
function indexOfAscii(buf: Uint8Array, needle: string, from = 0): number {
  const bytes = encoder.encode(needle);
  outer: for (let i = from; i <= buf.length - bytes.length; i++) {
    for (let j = 0; j < bytes.length; j++) {
      if (buf[i + j] !== bytes[j]) continue outer;
    }
    return i;
  }
  return -1;
}

// Fallback for WebRTCStream: reads the multipart/x-mixed-replace stream via fetch(), parses each
// JPEG part and draws it to a canvas.
export const MjpegStream: React.FC<WebRTCStreamProps> = ({
  sinkId,
  onStop,
  onError,
  className = '',
  compact = false,
}) => {
  const [connectionState, setConnectionState] = useState<'connecting' | 'connected' | 'failed'>('connecting');
  const [retryTick, setRetryTick] = useState(0);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const api = new ApiService();

  useEffect(() => {
    const controller = new AbortController();
    let cancelled = false;
    setConnectionState('connecting');

    const run = async () => {
      try {
        const response = await fetch(api.getMjpegStreamUrl(sinkId), { signal: controller.signal });
        if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`);
        const reader = response.body.getReader();
        let buffer = new Uint8Array(0);

        while (!cancelled) {
          const { done, value } = await reader.read();
          if (done) break;
          if (value && value.length > 0) {
            const merged = new Uint8Array(buffer.length + value.length);
            merged.set(buffer, 0);
            merged.set(value, buffer.length);
            buffer = merged;
          }

          // Drain every complete frame already buffered before reading more bytes.
          for (;;) {
            const boundaryIdx = indexOfAscii(buffer, BOUNDARY);
            if (boundaryIdx === -1) break;
            const headerStart = boundaryIdx + BOUNDARY.length;
            const headerEnd = indexOfAscii(buffer, HEADER_TERMINATOR, headerStart);
            if (headerEnd === -1) break; // headers not fully received yet

            const headerText = decoder.decode(buffer.slice(headerStart, headerEnd));
            const lengthMatch = headerText.match(/Content-Length:\s*(\d+)/i);
            if (!lengthMatch) {
              // malformed part: resync rather than spin
              buffer = buffer.slice(headerEnd + HEADER_TERMINATOR.length);
              continue;
            }

            const frameStart = headerEnd + HEADER_TERMINATOR.length;
            const frameEnd = frameStart + parseInt(lengthMatch[1], 10);
            if (buffer.length < frameEnd) break; // body not fully received yet

            const jpegBytes = buffer.slice(frameStart, frameEnd);
            buffer = buffer.slice(frameEnd);

            const bitmap = await createImageBitmap(new Blob([jpegBytes], { type: 'image/jpeg' }));
            const canvas = canvasRef.current;
            if (canvas) {
              if (canvas.width !== bitmap.width || canvas.height !== bitmap.height) {
                canvas.width = bitmap.width;
                canvas.height = bitmap.height;
              }
              canvas.getContext('2d')?.drawImage(bitmap, 0, 0);
            }
            bitmap.close();
            if (!cancelled) setConnectionState('connected');
          }
        }
      } catch (error) {
        if (cancelled) return;
        console.error('MjpegStream: stream failed:', error);
        setConnectionState('failed');
        onError(error instanceof Error ? error.message : 'MJPEG stream failed');
      }
    };

    run();
    return () => {
      cancelled = true;
      controller.abort();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sinkId, retryTick]);

  const stopStream = () => onStop();

  return (
    <div className={`bg-black rounded-lg overflow-hidden ${compact ? 'h-full' : ''} ${className}`}>
      {!compact && (
        <div className="p-4 bg-gray-800 flex justify-between items-center">
          <div className="flex items-center space-x-3">
            <Video className="w-5 h-5 text-white" />
            <span className="text-white font-medium">Sink {sinkId} Stream (MJPEG fallback)</span>
            <span className={`px-2 py-1 rounded text-xs text-white ${connectionState === 'connected' ? 'bg-green-600' : connectionState === 'failed' ? 'bg-red-600' : 'bg-yellow-600'} animate-pulse-slow flex items-center gap-1`}>
              <Activity className="w-3 h-3" />
              {connectionState === 'connected' ? 'Live' : connectionState === 'failed' ? 'Failed' : 'Connecting...'}
            </span>
          </div>
          <button
            onClick={stopStream}
            className="px-3 py-1 bg-red-600 text-white rounded text-sm hover:bg-red-700 transition-colors flex items-center gap-1"
            title="Stop stream"
          >
            <Square className="w-3 h-3" />
            Stop
          </button>
        </div>
      )}

      <div className={compact ? 'relative h-full' : 'relative'}>
        <canvas
          ref={canvasRef}
          className={compact ? 'w-full h-full object-cover bg-black' : 'w-full h-64 object-cover bg-black'}
          style={compact ? undefined : { aspectRatio: '16/9' }}
        />

        {connectionState === 'connecting' && (
          <div className="absolute inset-0 flex items-center justify-center bg-black bg-opacity-75">
            <div className="text-center text-white">
              <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-white mx-auto mb-2">
                <RefreshCw className="w-8 h-8" />
              </div>
              <p className="text-sm">Connecting to stream...</p>
            </div>
          </div>
        )}

        {connectionState === 'failed' && (
          <div className="absolute inset-0 flex items-center justify-center bg-red-900 bg-opacity-75">
            <div className="text-center text-white">
              <div className="flex items-center justify-center gap-2 mb-2">
                <XCircle className="w-5 h-5" />
                <p className="text-sm">Connection failed</p>
              </div>
              <button
                onClick={() => setRetryTick(t => t + 1)}
                className="mt-2 px-3 py-1 bg-white text-red-900 rounded text-sm hover:bg-gray-100 flex items-center gap-1"
              >
                <RefreshCw className="w-3 h-3" />
                Retry
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
