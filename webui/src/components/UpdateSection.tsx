import React, { useEffect, useRef, useState } from 'react';
import { ApiService } from '../services/ApiService';
import { SettingsSection } from './SettingsSection';
import type { StagedPackage, UpdateStatus } from '../types';

const api = new ApiService();

// Installs a newer (or older) lumenvision-backend .deb uploaded from this browser, with no internet on the board. The installer runs as root and
// restarts the server when it finishes, so this page loses its connection for a few seconds and reloads.
export const UpdateSection: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [staged, setStaged] = useState<StagedPackage | null>(null);
  const [uploading, setUploading] = useState(false);
  const [status, setStatus] = useState<UpdateStatus | null>(null);
  const [following, setFollowing] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null);

  useEffect(() => {
    api.getUpdateStatus().then(s => { setStatus(s); if (s.State === 'running') setFollowing(true); }).catch(() => { /* no update to report */ });
  }, []);

  // follow a running install; the server drops out while it restarts, which is expected
  useEffect(() => {
    if (!following) return;
    let reachable = true;
    const timer = setInterval(async () => {
      try {
        const next = await api.getUpdateStatus();
        reachable = true;
        setStatus(next);
        if (next.State === 'succeeded') {
          setFollowing(false);
          onToast('Update installed - reloading', 'success');
          setTimeout(() => window.location.reload(), 3000);
        } else if (next.State === 'failed') {
          setFollowing(false);
          onToast('The update failed - see the installer output', 'error');
        }
      } catch {
        if (reachable) setStatus(current => ({ State: 'running', Log: `${current?.Log ?? ''}\n(the server is restarting...)`.trim() }));
        reachable = false;
      }
    }, 2000);
    return () => clearInterval(timer);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [following]);

  const upload = async (file: File) => {
    setUploading(true);
    setStaged(null);
    try {
      setStaged(await api.uploadUpdatePackage(file));
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'The upload failed', 'error');
    } finally {
      setUploading(false);
    }
  };

  const install = async () => {
    if (!staged || !confirm(`Install ${staged.Package} ${staged.Version}? The server restarts, and cameras and streams stop for a minute.`)) return;
    try {
      await api.installUpdate();
      setStatus({ State: 'running', Log: '' });
      setFollowing(true);
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Could not start the install', 'error');
    }
  };

  return (
    <SettingsSection title="Update" hint="Upload a lumenvision-backend .deb (built for this board) to install it without internet. It is checked first, and nothing runs until you press Install.">
      <div className="flex gap-2 items-center">
        <button onClick={() => fileInput.current?.click()} disabled={uploading || following} className="px-3 py-2 bg-gray-700 text-white rounded text-sm hover:bg-gray-800 disabled:opacity-50">
          {uploading ? 'Uploading...' : 'Choose a .deb...'}
        </button>
        <input ref={fileInput} type="file" accept=".deb" className="hidden"
          onChange={e => { const file = e.target.files?.[0]; e.target.value = ''; if (file) upload(file); }} />
        {staged && (
          <>
            <span className="text-sm text-gray-700 dark:text-gray-300">{staged.Package} {staged.Version} ({staged.Architecture}, {(staged.SizeBytes / (1024 * 1024)).toFixed(1)} MB)</span>
            <button onClick={install} disabled={following} className="px-3 py-2 bg-blue-600 text-white rounded text-sm hover:bg-blue-700 disabled:opacity-50">Install</button>
          </>
        )}
      </div>
      {status && status.State !== 'idle' && (
        <div className="space-y-1">
          <div className="text-sm font-medium text-gray-900 dark:text-white">
            {status.State === 'running' ? 'Installing...' : status.State === 'succeeded' ? 'Installed' : 'Failed'}
          </div>
          {status.Log && <pre className="p-2 text-xs bg-gray-100 dark:bg-gray-900 rounded overflow-x-auto max-h-64 whitespace-pre-wrap">{status.Log}</pre>}
        </div>
      )}
    </SettingsSection>
  );
};
