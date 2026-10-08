import React, { useCallback, useEffect, useRef, useState } from 'react';
import { RefreshCw } from 'lucide-react';
import { WebRTCStream } from './WebRTCStream';
import { MjpegStream } from './MjpegStream';
import { MjpegThumbnail } from './MjpegThumbnail';
import { ApiService } from '../services/ApiService';
import type { WebRTCStreamProps } from '../types';

const api = new ApiService();

interface StreamViewProps extends WebRTCStreamProps {
  // node whose output the fallback MJPEG sink binds to; null means no fallback is possible
  sourceId: number | null;
}

type FallbackState = { kind: 'creating' } | { kind: 'ready'; sinkId: number } | { kind: 'failed' };

// Live previews use WebRTC; MJPEG is created only as a fallback when WebRTC negotiation fails.
export const StreamView: React.FC<StreamViewProps> = ({ sourceId, onError, sinkId, ...rest }) => {
  const [fallback, setFallback] = useState<FallbackState | null>(null);
  // A ref (set synchronously before any await) guards against duplicate WebRTC error callbacks
  // each creating their own MjpegSink.
  const fallbackStarted = useRef(false);
  // id of the fallback MjpegSink; held in a ref so cleanup sees the latest value. Deleted when the
  // preview target changes or unmounts.
  const fallbackSinkIdRef = useRef<number | null>(null);

  useEffect(() => {
    fallbackStarted.current = false;
    setFallback(null);
    return () => {
      const staleId = fallbackSinkIdRef.current;
      fallbackSinkIdRef.current = null;
      if (staleId != null) api.deleteSink(staleId).catch(() => {});
    };
  }, [sinkId]);

  // closing the tab or reloading skips the effect cleanup below, so delete the fallback sink as the page goes away
  useEffect(() => {
    const onPageHide = () => {
      const id = fallbackSinkIdRef.current;
      if (id != null) api.deleteSinkOnUnload(id);
    };
    window.addEventListener('pagehide', onPageHide);
    return () => window.removeEventListener('pagehide', onPageHide);
  }, []);

  const handleWebRtcError = useCallback(async (error: string) => {
    if (sourceId == null || fallbackStarted.current) {
      // no source to bind, or a fallback is already in flight/done - just surface the error
      onError(error);
      return;
    }
    fallbackStarted.current = true;
    console.warn(`StreamView: WebRTC preview failed (${error}), falling back to MJPEG`, { sinkId, sourceId });
    setFallback({ kind: 'creating' });
    let mjpegId: number | null = null;
    try {
      mjpegId = await api.createMjpegSink(`preview-mjpeg-${sourceId}`);
      fallbackSinkIdRef.current = mjpegId;
      await api.bindSinkToSource(mjpegId, sourceId);
      await api.toggleSink(mjpegId, true);
      setFallback({ kind: 'ready', sinkId: mjpegId });
    } catch (fallbackError) {
      console.error('StreamView: MJPEG fallback failed to start', fallbackError);
      if (mjpegId != null) {
        // partially set up: delete it now rather than rely on the cleanup effect
        fallbackSinkIdRef.current = null;
        api.deleteSink(mjpegId).catch(() => {});
      }
      setFallback({ kind: 'failed' });
      onError(error);
    }
  }, [sourceId, onError, sinkId]);


  if (fallback?.kind === 'ready') {
    // small views poll single frames; only a full-size view holds a stream connection (browsers allow about six per host)
    return rest.compact
      ? <MjpegThumbnail sinkId={fallback.sinkId} className={rest.className} />
      : <MjpegStream sinkId={fallback.sinkId} onError={onError} {...rest} />;
  }

  if (fallback?.kind === 'creating') {
    return (
      <div className={`bg-black rounded-lg overflow-hidden flex items-center justify-center ${rest.compact ? 'h-full' : 'h-64'} ${rest.className ?? ''}`}>
        <div className="text-center text-white">
          <RefreshCw className="w-6 h-6 mx-auto mb-2 animate-spin" />
          <p className="text-sm">WebRTC failed - starting MJPEG fallback...</p>
        </div>
      </div>
    );
  }

  return <WebRTCStream sinkId={sinkId} onError={handleWebRtcError} {...rest} />;
};
