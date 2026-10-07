import React, { useCallback, useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import { SettingsSection } from './SettingsSection';
import type { NetworkConnectionInfo, NetworkStatus } from '../types';

const api = new ApiService();
const field = 'w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white';

// One connection's IPv4 setup.
const ConnectionCard: React.FC<{
  connection: NetworkConnectionInfo;
  disabled: boolean;
  onApplied: () => void;
  onToast: (m: string, t: 'success' | 'error' | 'info') => void;
}> = ({ connection, disabled, onApplied, onToast }) => {
  const [method, setMethod] = useState(connection.Configured.Method);
  const [address, setAddress] = useState(connection.Configured.Address ?? '');
  const [gateway, setGateway] = useState(connection.Configured.Gateway ?? '');
  const [dns, setDns] = useState(connection.Configured.Dns.join(', '));

  const apply = async () => {
    const description = method === 'static' ? `the static address ${address}` : 'DHCP';
    if (!confirm(`Switch "${connection.Name}" to ${description}?\n\nThe device may become unreachable at its current address. If you do not confirm the change from the new address within a minute, it is undone.`)) return;
    try {
      await api.setIpv4(connection.Name, {
        Method: method,
        Address: method === 'static' ? address.trim() : null,
        Gateway: method === 'static' && gateway.trim() ? gateway.trim() : null,
        Dns: dns.split(',').map(s => s.trim()).filter(Boolean),
      });
      onToast('Network change applied - confirm it within a minute or it is undone', 'info');
      onApplied();
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to change the network setup', 'error');
    }
  };

  return (
    <div className="border border-gray-200 dark:border-gray-700 rounded-md p-3 space-y-3">
      <div className="flex items-center justify-between text-sm">
        <span className="font-medium text-gray-900 dark:text-white">{connection.Name} <span className="text-gray-500 dark:text-gray-400">({connection.Device})</span></span>
        <span className="text-xs text-gray-500 dark:text-gray-400">now {connection.CurrentAddresses.join(', ') || 'no address'}</span>
      </div>
      <div className="flex gap-4 text-sm">
        <label className="flex items-center gap-1"><input type="radio" checked={method === 'dhcp'} onChange={() => setMethod('dhcp')} />DHCP</label>
        <label className="flex items-center gap-1"><input type="radio" checked={method === 'static'} onChange={() => setMethod('static')} />Static</label>
      </div>
      {method === 'static' && (
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
          <input className={field} placeholder="10.12.34.11/24" value={address} onChange={e => setAddress(e.target.value)} aria-label="Address with prefix length" />
          <input className={field} placeholder="Gateway (optional)" value={gateway} onChange={e => setGateway(e.target.value)} />
          <input className={field} placeholder="DNS servers, comma separated" value={dns} onChange={e => setDns(e.target.value)} />
        </div>
      )}
      <button onClick={apply} disabled={disabled} className="px-3 py-1.5 bg-blue-600 text-white rounded text-sm hover:bg-blue-700 disabled:opacity-50">Apply</button>
    </div>
  );
};

// Hostname and IPv4 of the coprocessor. A change that is not confirmed from the new address is undone after a minute, so a typo cannot lock the device out.
export const NetworkSection: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [status, setStatus] = useState<NetworkStatus | null>(null);
  const [hostname, setHostname] = useState('');

  const refresh = useCallback(async () => {
    try {
      const next = await api.getNetwork();
      setStatus(next);
      setHostname(current => current || next.Hostname);
    } catch {
      /* the device may be changing address; the next poll will try again */
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  // count down a pending change while it waits
  const pending = status?.Pending ?? null;
  useEffect(() => {
    if (!pending) return;
    const timer = setInterval(refresh, 2000);
    return () => clearInterval(timer);
  }, [pending, refresh]);

  const saveHostname = async () => {
    try {
      await api.setHostname(hostname.trim());
      onToast(`Hostname set to ${hostname.trim()}`, 'success');
      refresh();
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to set the hostname', 'error');
    }
  };

  const settle = async (keep: boolean) => {
    try {
      if (keep) await api.confirmNetworkChange(); else await api.revertNetworkChange();
      onToast(keep ? 'Network change kept' : 'Network change undone', 'success');
    } catch {
      onToast('Failed to settle the network change', 'error');
    }
    refresh();
  };

  return (
    <SettingsSection title="Network" hint="The device's name and IP address. Changing the address can disconnect this page; the change is undone unless confirmed.">
      <div>
        <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Hostname (the device answers to {hostname || 'name'}.local)</label>
        <div className="flex gap-2">
          <input className={field} value={hostname} onChange={e => setHostname(e.target.value.toLowerCase())} />
          <button onClick={saveHostname} disabled={!hostname.trim() || hostname.trim() === status?.Hostname}
            className="px-3 py-2 bg-blue-600 text-white rounded text-sm hover:bg-blue-700 disabled:opacity-50">Rename</button>
        </div>
      </div>

      {pending && (
        <div className="p-3 rounded-md bg-amber-100 text-amber-900 dark:bg-amber-900 dark:text-amber-100 text-sm space-y-2">
          <div>"{pending.Connection}" changed. It will be undone in {pending.SecondsLeft} s unless you keep it. If this page loaded from the new address, the change works.</div>
          <div className="flex gap-2">
            <button onClick={() => settle(true)} className="px-3 py-1 bg-green-600 text-white rounded">Keep it</button>
            <button onClick={() => settle(false)} className="px-3 py-1 bg-gray-600 text-white rounded">Undo now</button>
          </div>
        </div>
      )}

      {status === null && <p className="text-sm text-gray-500 dark:text-gray-400">Reading the network setup...</p>}
      {status && !status.Supported && (
        <p className="text-sm text-gray-500 dark:text-gray-400">This device is managed by neither NetworkManager nor netplan, so the address cannot be changed here.</p>
      )}
      {status?.Supported && <p className="text-xs text-gray-500 dark:text-gray-400">Managed through {status.Mechanism}.</p>}
      {status?.Supported && status.Connections.length === 0 && <p className="text-sm text-gray-500 dark:text-gray-400">No ethernet interface found.</p>}
      {status?.Supported && status.Connections.map(connection => (
        <ConnectionCard key={`${connection.Name}-${connection.Configured.Method}-${connection.Configured.Address}`} connection={connection}
          disabled={pending !== null} onApplied={refresh} onToast={onToast} />
      ))}
    </SettingsSection>
  );
};
