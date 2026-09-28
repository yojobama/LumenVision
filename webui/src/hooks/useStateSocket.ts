import { useEffect, useRef, useState } from 'react';
import type { StateSnapshot } from '../types';

// Consumes the /ws/state push channel (Server/WebSockets/StateChannel.cs): one consolidated
// snapshot per server tick.
export function useStateSocket() {
  const [snapshot, setSnapshot] = useState<StateSnapshot | null>(null);
  const [connected, setConnected] = useState(false);
  const socketRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    let cancelled = false;
    let reconnectTimer: ReturnType<typeof setTimeout> | undefined;

    const connect = () => {
      if (cancelled) return;
      // ws:// or wss:// to match the page's protocol
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const socket = new WebSocket(`${protocol}//${window.location.host}/ws/state`);
      socketRef.current = socket;

      socket.onopen = () => setConnected(true);
      socket.onmessage = (event) => {
        try {
          setSnapshot(JSON.parse(event.data));
        } catch (err) {
          console.warn('useStateSocket: failed to parse snapshot', err);
        }
      };
      socket.onclose = () => {
        setConnected(false);
        if (!cancelled) reconnectTimer = setTimeout(connect, 2000);
      };
      socket.onerror = () => socket.close();
    };

    connect();

    return () => {
      cancelled = true;
      clearTimeout(reconnectTimer);
      socketRef.current?.close();
    };
  }, []);

  return { snapshot, connected };
}
