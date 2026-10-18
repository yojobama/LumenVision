import React, { useEffect, useState } from 'react';
import { ApiService } from '../services/ApiService';
import type { NodeProfile, StageStat } from '../types';

const api = new ApiService();

// Stages shown first, in this order, with what they mean; everything else follows alphabetically.
const LEAD_STAGES: [string, string][] = [
  ['age', 'Frame age when this node published its result: the latency shown on the node'],
  ['interval', 'Time between this node\'s results'],
  ['wait', 'How long the newest frame waited before this node started on it'],
  ['process', 'Time this node spent on a frame'],
];
const DESCRIPTIONS: Record<string, string> = Object.fromEntries(LEAD_STAGES);

const stageOrder = (stage: string): number => {
  const lead = LEAD_STAGES.findIndex(([name]) => name === stage);
  return lead >= 0 ? lead : LEAD_STAGES.length;
};

const ms = (value: number) => (value >= 100 ? value.toFixed(0) : value >= 10 ? value.toFixed(1) : value.toFixed(2));

// A node's per-stage timings over its last few seconds (GET /api/profile, LumenCore's StageProfile). Mean, 95th percentile and worst case, in ms.
export const TimingSection: React.FC<{ nodeId: number }> = ({ nodeId }) => {
  const [stages, setStages] = useState<StageStat[] | null>(null);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    if (!open) return;
    let cancelled = false;
    const load = () => api.getProfile()
      .then((nodes: NodeProfile[]) => { if (!cancelled) setStages(nodes.find(n => n.Id === nodeId)?.Stages ?? []); })
      .catch(() => { if (!cancelled) setStages(null); });
    load();
    const timer = setInterval(load, 2000);
    return () => { cancelled = true; clearInterval(timer); };
  }, [nodeId, open]);

  const sorted = (stages ?? []).slice().sort((a, b) => stageOrder(a.Stage) - stageOrder(b.Stage) || a.Stage.localeCompare(b.Stage));
  const slowest = Math.max(0.001, ...sorted.filter(s => s.Stage !== 'interval').map(s => s.MeanMs));

  return (
    <details className="text-xs" open={open} onToggle={e => setOpen((e.target as HTMLDetailsElement).open)}>
      <summary className="cursor-pointer select-none text-gray-500 dark:text-gray-400">Timing</summary>
      <div className="mt-2 space-y-1">
        {stages === null && <div className="text-gray-500 dark:text-gray-400">Reading...</div>}
        {stages !== null && sorted.length === 0 && <div className="text-gray-500 dark:text-gray-400">No timings yet - the node has not processed a frame.</div>}
        {sorted.length > 0 && (
          <table className="w-full text-gray-700 dark:text-gray-300">
            <thead>
              <tr className="text-left text-gray-500 dark:text-gray-400">
                <th className="font-normal">Stage</th>
                <th className="font-normal text-right">mean</th>
                <th className="font-normal text-right">p95</th>
                <th className="font-normal text-right">max</th>
              </tr>
            </thead>
            <tbody>
              {sorted.map(s => (
                <tr key={s.Stage} title={DESCRIPTIONS[s.Stage] ?? `${s.Stage} (${s.Count} samples)`}>
                  <td className={s.Stage.includes(':') ? 'pl-3 text-gray-500 dark:text-gray-400' : ''}>
                    <div>{s.Stage}</div>
                    {s.Stage !== 'interval' && (
                      <div className="h-1 rounded bg-blue-500/60" style={{ width: `${Math.min(100, (s.MeanMs / slowest) * 100)}%` }} />
                    )}
                  </td>
                  <td className="text-right tabular-nums align-top">{ms(s.MeanMs)}</td>
                  <td className="text-right tabular-nums align-top">{ms(s.P95Ms)}</td>
                  <td className="text-right tabular-nums align-top">{ms(s.MaxMs)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
        <div className="text-gray-500 dark:text-gray-400">Milliseconds over this node's last few seconds. Hover a row for what it measures.</div>
      </div>
    </details>
  );
};
