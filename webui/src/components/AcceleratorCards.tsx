import React from 'react';
import { Cpu } from 'lucide-react';
import type { AcceleratorInfo } from '../types';

const LABELS: Record<string, string> = { GPU: 'GPU', Memory: 'Memory bus', NPU: 'NPU' };

// Frequency, load and governor of the GPU, memory controller and NPU. Renders nothing on a machine that reports none.
export const AcceleratorCards: React.FC<{ accelerators: AcceleratorInfo[]; compact?: boolean }> = ({ accelerators, compact }) => {
  if (accelerators.length === 0) return null;
  return (
    <div className={`grid gap-4 ${compact ? 'grid-cols-2 md:grid-cols-4' : 'grid-cols-1 sm:grid-cols-3'}`}>
      {accelerators.map(a => (
        <div key={a.Name} className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 flex items-center gap-3"
          title={`${a.Name}, governor ${a.Governor || 'unknown'}, up to ${a.MaxFreqMhz.toFixed(0)} MHz`}>
          <Cpu className="w-6 h-6 text-teal-600" />
          <div>
            <div className="text-xs text-gray-500 dark:text-gray-400">{LABELS[a.Kind] ?? a.Name}{a.Governor ? ` - ${a.Governor}` : ''}</div>
            <div className="text-lg font-bold text-gray-900 dark:text-white">
              {a.FreqMhz.toFixed(0)} MHz{a.LoadPercent != null ? ` - ${a.LoadPercent.toFixed(0)}%` : ''}
            </div>
          </div>
        </div>
      ))}
    </div>
  );
};
