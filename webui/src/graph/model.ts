import type { Node, Edge } from '@xyflow/react';
import type { StateSnapshot, WsSinkState, WsSource, WsSink, NodeTypesResponse, NodeTypeCapability } from '../types';

// Enum ordinal to type-name tables, mirroring the declaration order of Server/Sink.cs's SinkType
// and Server/Source.cs's SourceType. SinkType index 2 is a reserved gap.
const SINK_TYPE_NAMES = [
  'ApriltagSink', 'ObjectDetectionSink', undefined, 'CameraCalibrationSink',
  'NetworkTablesSink', 'WebRTCSink', 'StereoCalibrationSink', 'StereoDepthSink', 'DepthFusionSink',
  'MjpegSink', 'RecordSink',
];
const SOURCE_TYPE_NAMES = ['Camera', 'ImageFile', 'VideoFile', 'SinkOutput'];

// Terminal/preview sinks (WebRTC, MJPEG, Record) render as badges on the node they are bound to.
const BADGE_SINK_TYPES = new Set(['WebRTCSink', 'NetworkTablesSink', 'MjpegSink', 'RecordSink']);

export function sinkTypeName(ordinal: number): string {
  return SINK_TYPE_NAMES[ordinal] ?? 'Unknown';
}
export function sourceTypeName(ordinal: number): string {
  return SOURCE_TYPE_NAMES[ordinal] ?? 'Unknown';
}

export interface PipelineNodeData extends Record<string, unknown> {
  kind: 'source' | 'sink';
  label: string;
  typeName: string;
  capability: NodeTypeCapability | null;
  fps: number;
  latencyUs: number;
  // the raw server object, read directly by the Inspector
  raw: WsSource | WsSink;
  // sink nodes only
  isRunning?: boolean;
  webrtcSink?: WsSinkState;
  nt4Sink?: WsSinkState;
  mjpegSink?: WsSinkState;
  recordSink?: WsSinkState;
  // source nodes only
  activeProfileIndex?: number;
  profileCount?: number;
}

export type PipelineNode = Node<PipelineNodeData, 'pipelineNode'>;

const COLUMN_WIDTH = 260;
const ROW_HEIGHT = 140;

// Positions are held by the caller (PositionStore) so the per-tick rebuild does not fight drags;
// unknown ids get an auto-layout slot.
export interface PositionStore {
  get(id: string): { x: number; y: number } | undefined;
  set(id: string, pos: { x: number; y: number }): void;
}

export function buildGraph(
  snapshot: StateSnapshot,
  capabilities: NodeTypesResponse | null,
  positions: PositionStore
): { nodes: PipelineNode[]; edges: Edge[] } {
  const nodes: PipelineNode[] = [];
  const edges: Edge[] = [];
  const sourceColumnCount: Record<string, number> = {};
  let sourceRow = 0;

  const findSourceCap = (ordinal: number) =>
    capabilities?.Sources.find(c => c.TypeName === sourceTypeName(ordinal)) ?? null;
  const findSinkCap = (ordinal: number) =>
    capabilities?.Sinks.find(c => c.TypeName === sinkTypeName(ordinal)) ?? null;

  const placeAt = (id: string, fallbackCol: number, fallbackRow: number) => {
    let pos = positions.get(id);
    if (!pos) {
      pos = { x: fallbackCol * COLUMN_WIDTH, y: fallbackRow * ROW_HEIGHT };
      positions.set(id, pos);
    }
    return pos;
  };

  // Source nodes (column 0)
  for (const source of snapshot.Sources) {
    const id = `source-${source.Id}`;
    const stats = snapshot.NodeStats[String(source.Id)];

    // badges: WebRTC/NT/MJPEG sinks can bind directly to a raw source as well as to a sink output
    const webrtcSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'WebRTCSink' && s.Sink.Source?.Id === source.Id);
    const nt4Sink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'NetworkTablesSink' && s.Sink.Source?.Id === source.Id);
    const mjpegSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'MjpegSink' && s.Sink.Source?.Id === source.Id);
    const recordSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'RecordSink' && s.Sink.Source?.Id === source.Id);

    nodes.push({
      id,
      type: 'pipelineNode',
      position: placeAt(id, 0, sourceRow),
      data: {
        kind: 'source',
        label: source.Name,
        typeName: sourceTypeName(source.Type),
        capability: findSourceCap(source.Type),
        fps: stats?.Fps ?? 0,
        latencyUs: stats?.LatencyUs ?? 0,
        raw: source,
        webrtcSink,
        nt4Sink,
        mjpegSink,
        recordSink,
        activeProfileIndex: source.ActiveProfileIndex,
        profileCount: source.Profiles.length,
      },
    });
    sourceRow++;
  }

  // Sink nodes (graph-shaped types only) and their binding edges; other sinks become badges.
  const graphSinks = snapshot.Sinks.filter(s => {
    const cap = findSinkCap(s.Sink.Type);
    return cap?.Implemented && !BADGE_SINK_TYPES.has(sinkTypeName(s.Sink.Type));
  });

  for (const sinkState of graphSinks) {
    const sink = sinkState.Sink;
    const id = `sink-${sink.Id}`;
    const stats = snapshot.NodeStats[String(sink.Id)];

    // badges bound to this sink's output (dual-role sinks also register as sources)
    const webrtcSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'WebRTCSink' && s.Sink.Source?.Id === sink.Id);
    const nt4Sink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'NetworkTablesSink' && s.Sink.Source?.Id === sink.Id);
    // same-preview MJPEG fallback badge
    const mjpegSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'MjpegSink' && s.Sink.Source?.Id === sink.Id);
    const recordSink = snapshot.Sinks.find(s => sinkTypeName(s.Sink.Type) === 'RecordSink' && s.Sink.Source?.Id === sink.Id);

    // column: one right of the primary (left, for stereo) source; column 1 if unbound
    const upstreamId = sink.Source ? `source-${sink.Source.Id}` : null;
    const upstreamPos = upstreamId ? positions.get(upstreamId) : undefined;
    const col = upstreamPos ? Math.round(upstreamPos.x / COLUMN_WIDTH) + 1 : 1;
    const row = sourceColumnCount[col] ?? 0;
    sourceColumnCount[col] = row + 1;

    nodes.push({
      id,
      type: 'pipelineNode',
      position: placeAt(id, col, row),
      data: {
        kind: 'sink',
        label: sink.Name,
        typeName: sinkTypeName(sink.Type),
        capability: findSinkCap(sink.Type),
        fps: stats?.Fps ?? 0,
        latencyUs: stats?.LatencyUs ?? 0,
        raw: sink,
        isRunning: sinkState.IsRunning,
        webrtcSink,
        nt4Sink,
        mjpegSink,
        recordSink,
      },
    });

    if (sink.Source && sink.Source2) {
      // stereo sink: Source is LEFT, Source2 is RIGHT
      edges.push({ id: `${id}-left`, source: `source-${sink.Source.Id}`, target: id, label: 'left', type: 'smoothstep' });
      edges.push({ id: `${id}-right`, source: `source-${sink.Source2.Id}`, target: id, label: 'right', type: 'smoothstep' });
    } else if (sink.Source) {
      edges.push({ id, source: `source-${sink.Source.Id}`, target: id, type: 'smoothstep' });
    }

    if (sink.DepthSourceId != null) {
      // DepthFusionSink depth attach: not an ordinary Source bind
      edges.push({ id: `${id}-depth`, source: `sink-${sink.DepthSourceId}`, target: id, label: 'depth', type: 'smoothstep' });
    }
  }

  return { nodes, edges };
}

// True when edges are unchanged; the caller then keeps its previous array reference so React Flow
// skips edge layout on every tick.
export function edgesEqual(a: Edge[], b: Edge[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i].id !== b[i].id || a[i].source !== b[i].source || a[i].target !== b[i].target || a[i].label !== b[i].label) {
      return false;
    }
  }
  return true;
}
