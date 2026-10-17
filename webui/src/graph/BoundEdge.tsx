import React, { useState } from 'react';
import { BaseEdge, EdgeLabelRenderer, getSmoothStepPath, type EdgeProps } from '@xyflow/react';
import { X } from 'lucide-react';

// What GraphPage hands every edge: detaches the binding the edge draws.
export interface BoundEdgeData extends Record<string, unknown> {
  onDisconnect?: (edgeId: string) => void;
}

// A smoothstep edge with a disconnect button at its midpoint, shown while the edge is hovered or selected. Together with dragging an edge's end off its handle and Delete on a selected edge, it gives
// disconnecting the same one-gesture ease as dragging a connection out.
export const BoundEdge: React.FC<EdgeProps> = ({
  id, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, label, selected, markerEnd, style, data,
}) => {
  const [hovered, setHovered] = useState(false);
  const [path, labelX, labelY] = getSmoothStepPath({ sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition });
  const onDisconnect = (data as BoundEdgeData | undefined)?.onDisconnect;
  const showButton = !!onDisconnect && (hovered || selected);

  return (
    <g onMouseEnter={() => setHovered(true)} onMouseLeave={() => setHovered(false)}>
      <BaseEdge id={id} path={path} markerEnd={markerEnd}
        style={{ ...style, strokeWidth: hovered || selected ? 3 : 2, stroke: selected ? '#3b82f6' : undefined }} />
      {/* a wide invisible stroke so the thin line is easy to hover and click */}
      <path d={path} fill="none" stroke="transparent" strokeWidth={20} className="react-flow__edge-interaction" />
      <EdgeLabelRenderer>
        <div
          className="nodrag nopan absolute flex items-center gap-1"
          style={{ transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)`, pointerEvents: 'all' }}
          onMouseEnter={() => setHovered(true)}
          onMouseLeave={() => setHovered(false)}
        >
          {label && (
            <span className="px-1.5 py-0.5 rounded bg-white dark:bg-gray-800 text-xs text-gray-700 dark:text-gray-200 border border-gray-300 dark:border-gray-600">
              {label}
            </span>
          )}
          {showButton && (
            <button
              type="button"
              aria-label="Disconnect"
              title="Disconnect"
              onClick={(e) => { e.stopPropagation(); onDisconnect?.(id); }}
              className="w-5 h-5 rounded-full bg-red-600 hover:bg-red-700 text-white flex items-center justify-center shadow"
            >
              <X className="w-3 h-3" />
            </button>
          )}
        </div>
      </EdgeLabelRenderer>
    </g>
  );
};

export const edgeTypes = { bound: BoundEdge };
