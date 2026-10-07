import React, { useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import { SettingsSection } from './SettingsSection';
import type { VersionInfo } from '../types';

const api = new ApiService();

// What this coprocessor is running.
export const AboutSection: React.FC = () => {
  const [version, setVersion] = useState<VersionInfo | null>(null);

  useEffect(() => {
    api.getVersion().then(setVersion).catch(() => setVersion(null));
  }, []);

  if (!version) return null;
  const rows: Array<[string, string]> = [
    ['LumenVision server', version.Server],
    ['LumenCore', version.LumenCore],
    ['Operating system', version.Os],
    ['Kernel', version.Kernel],
    ['Architecture', version.Architecture],
    ['Runtime', version.Runtime],
    ['Hostname', version.Hostname],
  ];
  return (
    <SettingsSection title="About" hint="Quote these when asking for help. The robot library warns when its version differs from LumenCore's.">
      <dl className="grid grid-cols-3 gap-y-1 text-sm">
        {rows.map(([label, value]) => (
          <React.Fragment key={label}>
            <dt className="text-gray-500 dark:text-gray-400">{label}</dt>
            <dd className="col-span-2 text-gray-900 dark:text-white break-all">{value}</dd>
          </React.Fragment>
        ))}
      </dl>
    </SettingsSection>
  );
};
