import React from 'react';

export const ToggleSwitch: React.FC<{
  enabled: boolean;
  onChange: (enabled: boolean) => void;
  disabled?: boolean;
}> = ({ enabled, onChange, disabled = false }) => {
  return (
    <button
      onClick={() => !disabled && onChange(!enabled)}
      disabled={disabled}
      className={`relative inline-flex items-center h-6 rounded-full w-11 transition-colors focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-blue-500 ${
        disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'
      } ${
        enabled
          ? 'bg-green-600 dark:bg-green-500'
          : 'bg-gray-300 dark:bg-gray-600'
      }`}
      title={enabled ? 'Enabled - click to disable' : 'Disabled - click to enable'}
    >
      <span
        className={`inline-block w-4 h-4 transform transition-transform bg-white rounded-full ${
          enabled ? 'translate-x-6' : 'translate-x-1'
        }`}
      />
    </button>
  );
};
