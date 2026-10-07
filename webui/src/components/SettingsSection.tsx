import React from 'react';

// A titled block of the settings page.
export const SettingsSection: React.FC<{ title: string; hint?: string; children: React.ReactNode }> = ({ title, hint, children }) => (
  <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6 space-y-4">
    <div>
      <h3 className="text-lg font-semibold text-gray-900 dark:text-white">{title}</h3>
      {hint && <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">{hint}</p>}
    </div>
    {children}
  </div>
);
