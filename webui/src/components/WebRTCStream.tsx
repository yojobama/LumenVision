import React, { useState, useEffect, useRef } from 'react';
import {
  RefreshCw,
  Minimize2,
  Maximize2,
  Square,
  Video,
  Activity,
  XCircle
} from 'lucide-react';
import { WebRTCStreamProps } from '../types';
import { ApiService } from '../services/ApiService';

const DISCONNECT_GRACE_MS = 5000;

export const WebRTCStream: React.FC<WebRTCStreamProps> = ({
  sinkId,
  onStop,
  onError,
  className = '',
  compact = false,
}) => {
  const [connectionState, setConnectionState] = useState<string>('connecting');
  const [isFullscreen, setIsFullscreen] = useState(false);
  const videoRef = useRef<HTMLVideoElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  // a ref, not state: the mount-time effect's cleanup closure would otherwise read a stale null
  // and never close the peer connection
  const peerConnectionRef = useRef<RTCPeerConnection | null>(null);
  const disconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const api = new ApiService();

  useEffect(() => {
    startWebRTCConnection();
    return () => {
      if (disconnectTimer.current) clearTimeout(disconnectTimer.current);
      disconnectTimer.current = null;
      peerConnectionRef.current?.close();
      peerConnectionRef.current = null;
    };
  }, [sinkId]);

  const startWebRTCConnection = async () => {
    try {
      setConnectionState('connecting');

      // No STUN server: the browser and coprocessor share a LAN, so no NAT traversal is needed.
      const config: RTCConfiguration = { iceServers: [] };

      const pc = new RTCPeerConnection(config);
      peerConnectionRef.current = pc;

      pc.onconnectionstatechange = () => {
        setConnectionState(pc.connectionState);
        if (disconnectTimer.current) {
          clearTimeout(disconnectTimer.current);
          disconnectTimer.current = null;
        }
        if (pc.connectionState === 'failed') {
          onError('Connection failed');
        } else if (pc.connectionState === 'disconnected') {
          // ICE reports "disconnected" for a brief network hiccup and usually recovers by itself; only give up (and fall back to MJPEG) if it stays down
          disconnectTimer.current = setTimeout(() => {
            if (pc.connectionState === 'disconnected') onError('Connection disconnected');
          }, DISCONNECT_GRACE_MS);
        }
      };

      pc.ontrack = (event) => {
        if (videoRef.current && event.streams[0]) {
          videoRef.current.srcObject = event.streams[0];
          setConnectionState('connected');
        }
      };

      // The server uses non-trickle ICE, and libdatachannel throws if a remote candidate arrives
      // before the answer, so buffer candidates and flush them after the answer POST completes.
      let answerSent = false;
      const pendingCandidates: RTCIceCandidate[] = [];
      pc.onicecandidate = async (event) => {
        if (!event.candidate) return;
        if (!answerSent) {
          pendingCandidates.push(event.candidate);
          return;
        }
        try {
          await api.sendWebRTCIceCandidate(sinkId, event.candidate.candidate, event.candidate.sdpMid || '');
        } catch (err) {
          console.warn('Failed to send ICE candidate:', err);
        }
      };

      // getWebRTCOffer returns a plain SDP string (not JSON) after server-side ICE gathering
      const offerSdp = await api.getWebRTCOffer(sinkId);
      await pc.setRemoteDescription({ type: 'offer', sdp: offerSdp });
      const answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      await api.sendWebRTCAnswer(sinkId, answer.sdp || '');
      answerSent = true;
      for (const candidate of pendingCandidates) {
        try {
          await api.sendWebRTCIceCandidate(sinkId, candidate.candidate, candidate.sdpMid || '');
        } catch (err) {
          console.warn('Failed to send buffered ICE candidate:', err);
        }
      }
    } catch (error) {
      console.error('WebRTC connection failed:', error);
      setConnectionState('failed');
      onError(error instanceof Error ? error.message : 'Connection failed');
    }
  };

  const stopStream = () => {
    // No server-side stop endpoint exists; closing the local peer connection is sufficient.
    peerConnectionRef.current?.close();
    peerConnectionRef.current = null;
    onStop();
  };

  const toggleFullscreen = () => {
    if (!document.fullscreenElement && containerRef.current) {
      containerRef.current.requestFullscreen();
      setIsFullscreen(true);
    } else if (document.fullscreenElement) {
      document.exitFullscreen();
      setIsFullscreen(false);
    }
  };

  useEffect(() => {
    const handleFullscreenChange = () => {
      setIsFullscreen(!!document.fullscreenElement);
    };
    document.addEventListener('fullscreenchange', handleFullscreenChange);
    return () => document.removeEventListener('fullscreenchange', handleFullscreenChange);
  }, []);

  const getStatuscolour = (state: string) => {
    switch (state) {
      case 'connected': return 'bg-green-600';
      case 'connecting': return 'bg-yellow-600';
      case 'failed': return 'bg-red-600';
      default: return 'bg-gray-600';
    }
  };

  const getStatusText = (state: string) => {
    switch (state) {
      case 'connected': return 'Live';
      case 'connecting': return 'Connecting...';
      case 'failed': return 'Failed';
      default: return 'Unknown';
    }
  };

  return (
    <div ref={containerRef} className={`bg-black rounded-lg overflow-hidden ${compact ? 'h-full' : ''} ${className}`}>
      {!compact && (
      <div className="p-4 bg-gray-800 flex justify-between items-center">
        <div className="flex items-center space-x-3">
          <Video className="w-5 h-5 text-white" />
          <span className="text-white font-medium">Sink {sinkId} Stream</span>
          <span className={`px-2 py-1 rounded text-xs text-white ${getStatuscolour(connectionState)} animate-pulse-slow flex items-center gap-1`}>
            <Activity className="w-3 h-3" />
            {getStatusText(connectionState)}
          </span>
        </div>

        <div className="flex items-center gap-2">
          <button
            onClick={toggleFullscreen}
            className="px-3 py-1 bg-blue-600 text-white rounded text-sm hover:bg-blue-700 transition-colors flex items-center gap-1"
            title="Toggle fullscreen"
          >
            {isFullscreen ? <Minimize2 className="w-3 h-3" /> : <Maximize2 className="w-3 h-3" />}
          </button>
          <button
            onClick={stopStream}
            className="px-3 py-1 bg-red-600 text-white rounded text-sm hover:bg-red-700 transition-colors flex items-center gap-1"
            title="Stop stream"
          >
            <Square className="w-3 h-3" />
            Stop
          </button>
        </div>
      </div>
      )}

      <div className={compact ? 'relative h-full' : 'relative'}>
        <video
          ref={videoRef}
          autoPlay
          muted
          playsInline
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
                onClick={startWebRTCConnection}
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