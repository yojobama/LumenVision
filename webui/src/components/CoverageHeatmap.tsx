import React, { useEffect, useRef } from 'react';
import type { CalibrationCoverage } from '../types';

// Plots each snapshot's detected corners (flat [x0,y0,x1,y1,...]) over the frame bounds;
// regions with no nearby dots have not yet been covered by the checkerboard.
export const CoverageHeatmap: React.FC<{ coverage: CalibrationCoverage | null; className?: string }> = ({ coverage, className = '' }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    ctx.clearRect(0, 0, width, height);

    // background + frame border
    ctx.fillStyle = 'rgba(107, 114, 128, 0.15)'; // gray-500 @ 15%
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = 'rgba(107, 114, 128, 0.5)';
    ctx.strokeRect(0.5, 0.5, width - 1, height - 1);

    if (!coverage || coverage.FrameWidth <= 0 || coverage.FrameHeight <= 0) return;

    const scaleX = width / coverage.FrameWidth;
    const scaleY = height / coverage.FrameHeight;

    // Each snapshot's corners as a connected outline plus a dot per corner.
    coverage.Snapshots.forEach((flat, i) => {
      if (flat.length < 4) return;
      const hue = (i * 47) % 360; // spread distinct snapshots across the colour wheel
      ctx.strokeStyle = `hsla(${hue}, 70%, 55%, 0.5)`;
      ctx.fillStyle = `hsla(${hue}, 70%, 55%, 0.7)`;
      ctx.lineWidth = 1;

      ctx.beginPath();
      for (let p = 0; p + 1 < flat.length; p += 2) {
        const x = flat[p] * scaleX;
        const y = flat[p + 1] * scaleY;
        if (p === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.closePath();
      ctx.stroke();

      for (let p = 0; p + 1 < flat.length; p += 2) {
        const x = flat[p] * scaleX;
        const y = flat[p + 1] * scaleY;
        ctx.beginPath();
        ctx.arc(x, y, 2, 0, Math.PI * 2);
        ctx.fill();
      }
    });
  }, [coverage]);

  return (
    <div className={className}>
      <canvas ref={canvasRef} width={480} height={360} className="w-full h-auto bg-gray-100 dark:bg-gray-900 rounded border border-gray-300 dark:border-gray-600" />
      <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">
        {coverage ? `${coverage.Snapshots.length} snapshot${coverage.Snapshots.length === 1 ? '' : 's'} · ${coverage.FrameWidth}×${coverage.FrameHeight}` : 'No coverage data yet'}
      </p>
    </div>
  );
};
