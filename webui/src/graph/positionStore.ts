import type { PositionStore } from './model';

// Where the graph's nodes sit, kept outside the Graph page so leaving it for another tab (which unmounts the page) does not
// forget the layout, and in localStorage so a reload does not either. Per browser: node ids are the server's, stable across restarts.
const STORAGE_KEY = 'lumenvision.graph.positions';

type Position = { x: number; y: number };

function load(): Map<string, Position> {
  const positions = new Map<string, Position>();
  try {
    const stored = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? '{}') as Record<string, Position>;
    for (const [id, pos] of Object.entries(stored)) {
      if (pos && Number.isFinite(pos.x) && Number.isFinite(pos.y)) positions.set(id, { x: pos.x, y: pos.y });
    }
  } catch {
    // storage unavailable or corrupt: start from the automatic layout
  }
  return positions;
}

const positions = load();
let saveTimer: ReturnType<typeof setTimeout> | undefined;

// drags move a node many times a second, so writes are batched
function scheduleSave() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(Object.fromEntries(positions)));
    } catch {
      // private window or quota: the in-memory layout still holds for this session
    }
  }, 300);
}

export const graphPositions: PositionStore & { retainOnly(ids: Set<string>): void } = {
  get: (id) => positions.get(id),
  set: (id, pos) => {
    const current = positions.get(id);
    if (current && current.x === pos.x && current.y === pos.y) return;
    positions.set(id, { x: pos.x, y: pos.y });
    scheduleSave();
  },
  // forgets the nodes that no longer exist, so deleted ones do not pile up
  retainOnly: (ids) => {
    let removed = false;
    for (const id of [...positions.keys()]) {
      if (!ids.has(id)) { positions.delete(id); removed = true; }
    }
    if (removed) scheduleSave();
  },
};
