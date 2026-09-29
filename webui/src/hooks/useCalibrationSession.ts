import { useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import type { CalibrationSession } from '../types';

const api = new ApiService();

// A stop is delayed so React's dev-mode unmount/remount (and quick route changes back) keep the session alive.
const STOP_DELAY_MS = 2000;
const pendingStops = new Map<number, ReturnType<typeof setTimeout>>();

export type SessionState =
  | { status: 'loading' }
  | { status: 'missing' }
  | { status: 'ready'; session: CalibrationSession };

// Loads a calibration session and stops it on the server when the wizard is left.
export function useCalibrationSession(sessionId: number, kind: CalibrationSession['Kind']): SessionState {
  const [state, setState] = useState<SessionState>({ status: 'loading' });

  useEffect(() => {
    const pending = pendingStops.get(sessionId);
    if (pending) {
      clearTimeout(pending);
      pendingStops.delete(sessionId);
    }

    let cancelled = false;
    setState({ status: 'loading' });
    api.getCalibrationSession(sessionId)
      .then(session => {
        if (cancelled) return;
        setState(session.Kind === kind ? { status: 'ready', session } : { status: 'missing' });
      })
      .catch(() => { if (!cancelled) setState({ status: 'missing' }); });

    return () => {
      cancelled = true;
      pendingStops.set(sessionId, setTimeout(() => {
        pendingStops.delete(sessionId);
        api.stopCalibrationSession(sessionId).catch(() => {});
      }, STOP_DELAY_MS));
    };
  }, [sessionId, kind]);

  return state;
}
