import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  ReactFlow, ReactFlowProvider, Background, Controls, MiniMap,
  useNodesState, type Edge, type Connection, type IsValidConnection,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { Plus, Camera as CameraIcon, Target } from 'lucide-react';
import { useStateSocket } from '../hooks/useStateSocket';
import { ApiService } from '../services/ApiService';
import { buildGraph, edgesEqual, type PipelineNode } from '../graph/model';
import { nodeTypes } from '../graph/PipelineNode';
import { graphPositions } from '../graph/positionStore';
import { edgeTypes, type BoundEdgeData } from '../graph/BoundEdge';
import { Inspector } from '../graph/Inspector';
import { GraphProfileBar } from '../graph/GraphProfileBar';
import { LeftRail } from '../graph/LeftRail';
import { BottomStrip } from '../graph/BottomStrip';
import { AddSourceModal } from '../components/AddSourceModal';
import { AddSinkModal } from '../components/AddSinkModal';
import { deleteNode, duplicateNode } from '../graph/nodeActions';
import type { AddSinkOptions, NodeTypesResponse, CameraHardwareInfo } from '../types';

const api = new ApiService();

// The pipeline graph: sources and graph-shaped sinks are nodes, edges are live bindings from
// /ws/state, and dragging a connection performs the bind via REST.
const GraphPageInner: React.FC<{ onToast: (m: string, t: 'success'|'error'|'info') => void; darkMode: boolean }> = ({ onToast, darkMode }) => {
  const { snapshot, connected } = useStateSocket();
  const [capabilities, setCapabilities] = useState<NodeTypesResponse | null>(null);
  const [nodes, setNodes, onNodesChange] = useNodesState<PipelineNode>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [selectedEdgeId, setSelectedEdgeId] = useState<string | null>(null);
  const [showAddSource, setShowAddSource] = useState(false);
  const [showAddSink, setShowAddSink] = useState(false);

  useEffect(() => {
    api.getNodeTypeCapabilities().then(setCapabilities).catch(() => onToast('Failed to load node capabilities', 'error'));
  }, []);

  // sync the position store with React Flow's drags so the next rebuild (and the next visit to this page) keeps them
  useEffect(() => {
    for (const n of nodes) graphPositions.set(n.id, n.position);
  }, [nodes]);

  useEffect(() => {
    if (!snapshot) return;
    const built = buildGraph(snapshot, capabilities, graphPositions);
    graphPositions.retainOnly(new Set(built.nodes.map(n => n.id)));
    setNodes(built.nodes);
    // reuse the previous array reference when the edge set is unchanged (see edgesEqual)
    setEdges(prev => (edgesEqual(prev, built.edges) ? prev : built.edges));
  }, [snapshot, capabilities, setNodes]);

  const selectedNode = nodes.find(n => n.id === selectedId) ?? null;

  // Keyboard shortcuts on the selected node: Ctrl+C remembers it, Ctrl+V copies it on the server (Ctrl+Shift+V also keeps its source
  // binding), F2 renames it in the Inspector and Delete removes it. Ignored while typing in a field.
  const copiedRef = useRef<PipelineNode | null>(null);
  const shortcutState = useRef<{ selectedNode: PipelineNode | null; onToast: typeof onToast; selectedEdgeId: string | null; disconnect: (edgeId: string) => Promise<void> }>(
    { selectedNode, onToast, selectedEdgeId: null, disconnect: async () => {} });
  shortcutState.current = { selectedNode, onToast, selectedEdgeId, disconnect: (edgeId) => disconnectEdge(edgeId) };
  useEffect(() => {
    const handler = async (e: KeyboardEvent) => {
      const target = e.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.tagName === 'SELECT' || target.isContentEditable)) return;
      const { selectedNode: current, onToast: toast, selectedEdgeId: edgeId, disconnect } = shortcutState.current;
      const modifier = e.ctrlKey || e.metaKey;

      try {
        if (modifier && e.key.toLowerCase() === 'c' && current) {
          copiedRef.current = current;
          toast(`Copied "${current.data.label}"`, 'info');
        } else if (modifier && e.key.toLowerCase() === 'v' && copiedRef.current) {
          e.preventDefault();
          const original = copiedRef.current;
          const newId = await duplicateNode(original, e.shiftKey);
          const origin = graphPositions.get(original.id) ?? original.position;
          graphPositions.set(newId, { x: origin.x + 40, y: origin.y + 60 });
          setSelectedId(newId);
          toast(`Pasted a copy of "${original.data.label}"`, 'success');
        } else if (e.key === 'F2' && current) {
          e.preventDefault();
          const field = document.getElementById('inspector-name') as HTMLInputElement | null;
          field?.focus();
          field?.select();
        } else if ((e.key === 'Delete' || e.key === 'Backspace') && edgeId) {
          // a selected connection: only the connection goes, never a node that happens to be selected too
          e.preventDefault();
          await disconnect(edgeId);
        } else if ((e.key === 'Delete' || e.key === 'Backspace') && current) {
          e.preventDefault();
          if (await deleteNode(current)) {
            setSelectedId(null);
            toast('Deleted', 'info');
          }
        }
      } catch (error) {
        toast(error instanceof Error && error.message ? error.message : 'That action failed', 'error');
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);
  // A WebRTCSink holds one peer connection, so the sink shown in the Inspector is excluded from
  // BottomStrip to avoid double negotiation. Derived from `snapshot` (not `nodes`, which lags a
  // render) so both see identical data.
  const selectedRawId = selectedId ? Number(selectedId.split('-')[1]) : null;
  const inspectorPreviewSinkId = selectedRawId != null && snapshot
    ? snapshot.Sinks.find(s => s.Sink.Type === 5 && s.IsRunning && s.Sink.Source?.Id === selectedRawId)?.Sink.Id ?? null
    : null;

  const onConnect = useCallback(async (connection: Connection) => {
    // source->sink: bind. sink->sink (StereoDepthSink output to a DepthFusionSink): attach as depth source.
    const [sourceKind, sourceRawId] = connection.source.split('-');
    const [targetKind, targetRawId] = connection.target.split('-');
    if (targetKind !== 'sink') return;
    const targetSinkId = Number(targetRawId);

    try {
      if (sourceKind === 'source') {
        const targetNode = nodes.find(n => n.id === connection.target);
        if (targetNode?.data.capability?.SourceRoles) {
          // stereo sink: ask which role this connection fills
          const role = prompt(`Bind as "left" or "right" camera?`, 'left');
          if (role !== 'left' && role !== 'right') return;
          // the bind call needs both ids: reuse the already-bound role, if any, as the other side
          const existingEdge = edges.find(e => e.target === connection.target);
          const existingSourceId = existingEdge ? Number(existingEdge.source.split('-')[1]) : undefined;
          const leftId = role === 'left' ? Number(sourceRawId) : existingSourceId;
          const rightId = role === 'right' ? Number(sourceRawId) : existingSourceId;
          if (leftId == null || rightId == null) {
            onToast('Bind the other camera role first, or use a single-source sink', 'error');
            return;
          }
          await api.bindStereoDepthSources(targetSinkId, leftId, rightId);
        } else {
          await api.bindSinkToSource(targetSinkId, Number(sourceRawId));
        }
        onToast('Connected', 'success');
      } else if (sourceKind === 'sink') {
        // depth attach: a StereoDepthSink feeding a DepthFusionSink's depth input
        await api.attachDepthFusionSource(targetSinkId, Number(sourceRawId));
        onToast('Depth source attached', 'success');
      }
    } catch {
      onToast('Failed to connect - check the connection is valid for these node types', 'error');
    }
  }, [nodes, edges, onToast]);

  // Detaches what an edge draws. Edge ids come from graph/model.ts: "<sink>" (one source), "<sink>-left"/"-right" (stereo cameras;
  // the server unbinds both) and "<sink>-depth" (a DepthFusionSink's depth source).
  const disconnectEdge = useCallback(async (edgeId: string) => {
    const edge = edges.find(e => e.id === edgeId);
    if (!edge) return;
    if (edgeId.startsWith('crop-')) {
      onToast('A crop is always fed by its camera - delete the crop node to remove it', 'info');
      return;
    }
    const targetSinkId = Number(edge.target.split('-')[1]);
    try {
      if (edgeId.endsWith('-depth')) {
        await api.detachDepthFusionSource(targetSinkId);
        onToast('Depth source detached', 'success');
      } else {
        await api.unbindSinkFromSource(targetSinkId);
        onToast(edgeId.endsWith('-left') || edgeId.endsWith('-right') ? 'Disconnected both cameras' : 'Disconnected', 'success');
      }
      setSelectedEdgeId(null);
    } catch {
      onToast('Failed to disconnect', 'error');
    }
  }, [edges, onToast]);

  // Dragging an edge's end off its handle: onto another valid node it rebinds (single-source edges), onto empty canvas it detaches.
  const reconnectedRef = useRef(false);
  const onReconnectStart = useCallback(() => { reconnectedRef.current = false; }, []);
  const onReconnect = useCallback(async (oldEdge: Edge, connection: Connection) => {
    reconnectedRef.current = true;
    const plain = !oldEdge.id.endsWith('-left') && !oldEdge.id.endsWith('-right') && !oldEdge.id.endsWith('-depth');
    if (!plain || !connection.source || !connection.target) return; // stereo/depth edges cannot move; dropping them on a handle keeps them
    if (connection.source === oldEdge.source && connection.target === oldEdge.target) return;
    try {
      const oldTarget = Number(oldEdge.target.split('-')[1]);
      const newTarget = Number(connection.target.split('-')[1]);
      const newSource = Number(connection.source.split('-')[1]);
      if (newTarget !== oldTarget) await api.unbindSinkFromSource(oldTarget);
      await api.bindSinkToSource(newTarget, newSource);
      onToast('Reconnected', 'success');
    } catch {
      onToast('Failed to reconnect', 'error');
    }
  }, [onToast]);
  const onReconnectEnd = useCallback((_: unknown, edge: Edge) => {
    if (!reconnectedRef.current) void disconnectEdge(edge.id);
  }, [disconnectEdge]);

  // every edge carries the disconnect action its button calls
  const edgesWithActions = useMemo(
    () => edges.map(e => ({ ...e, selected: e.id === selectedEdgeId, data: { onDisconnect: e.id.startsWith('crop-') ? undefined : (id: string) => void disconnectEdge(id) } as BoundEdgeData })),
    [edges, selectedEdgeId, disconnectEdge]);

  const isValidConnection: IsValidConnection = useCallback((edgeOrConn) => {
    const sourceId = 'source' in edgeOrConn ? edgeOrConn.source : undefined;
    const targetId = 'target' in edgeOrConn ? edgeOrConn.target : undefined;
    if (!sourceId || !targetId || sourceId === targetId) return false;
    const targetNode = nodes.find(n => n.id === targetId);
    const sourceNode = nodes.find(n => n.id === sourceId);
    if (!targetNode || !sourceNode || targetNode.data.kind !== 'sink') return false;

    const cap = targetNode.data.capability;
    if (!cap) return false;

    if (cap.HasDepthAttach) {
      // DepthFusionSink: depth input accepts only a StereoDepthSink's output
      return sourceNode.data.kind === 'sink' && sourceNode.data.typeName === 'StereoDepthSink';
    }

    // ordinary/stereo bind: source must be a real Source or a dual-role sink acting as one
    if (sourceNode.data.kind === 'sink' && !sourceNode.data.capability?.IsDualRoleSink) return false;

    const existingCount = edges.filter(e => e.target === targetId).length;
    return existingCount < cap.MaxSources;
  }, [nodes, edges]);

  const placeNewNode = () => {
    // staggered placement for new nodes not yet in the snapshot
    const count = nodes.length;
    return { x: (count % 4) * 260, y: Math.floor(count / 4) * 260 + 400 };
  };

  const handleAddSource = async (name: string, type: string, files?: FileList, fps?: number, hardwareInfo?: CameraHardwareInfo, split?: { parentId: number; layout: 'sideBySide' | 'topBottom' }) => {
    try {
      if (type === 'split' && split) {
        await api.splitCamera(split.parentId, split.layout);
        onToast(split.layout === 'sideBySide' ? 'Camera split into left and right' : 'Camera split into top and bottom', 'success');
        setShowAddSource(false);
        return;
      }
      if (type === 'camera' && hardwareInfo) {
        await api.createCameraSource(hardwareInfo, name);
      } else if (type === 'video' && files?.length) {
        const result = await api.uploadVideoFiles(files, fps ?? 30);
        if (!result.success) { onToast(result.message, 'error'); return; }
      } else if (type === 'image' && files?.length) {
        const result = await api.uploadImageFiles(files);
        if (!result.success) { onToast(result.message, 'error'); return; }
      }
      onToast(`Source "${name}" added`, 'success');
      setShowAddSource(false);
    } catch {
      onToast('Failed to add source', 'error');
    }
  };

  const handleAddSink = async (name: string, type: string, options?: AddSinkOptions) => {
    try {
      if (type === 'ApriltagSink') {
        await api.createApriltagSinkWithBackend(name, options?.tagSize ?? 0.1651, options?.backend ?? 0);
      } else if (type === 'object' && options?.newModel) {
        // upload a new model first, then create the sink from the returned model id; a failed upload throws
        const modelId = await api.uploadModel(options.newModel);
        await api.createObjectDetectionSink(name, modelId);
      } else if (type === 'object' && options?.modelId) {
        await api.createObjectDetectionSink(name, options.modelId,
          options.confThreshold !== undefined && options.nmsThreshold !== undefined
            ? { confThreshold: options.confThreshold, nmsThreshold: options.nmsThreshold }
            : undefined);
      }
      onToast(`Sink "${name}" added`, 'success');
      setShowAddSink(false);
    } catch {
      onToast('Failed to add sink', 'error');
    }
  };

  return (
    <div className="flex h-[calc(100vh-140px)] -m-6">
      <LeftRail snapshot={snapshot} onToast={onToast} />
      {/* flex column so BottomStrip sits below the canvas without overlapping its controls */}
      <div className="flex-1 flex flex-col min-h-0">
        <div className="flex-1 relative min-h-0">
          <div className="absolute top-4 left-4 z-10 flex gap-2">
            <button onClick={() => setShowAddSource(true)} className="px-3 py-2 bg-blue-600 text-white rounded shadow hover:bg-blue-700 flex items-center gap-2 text-sm">
              <CameraIcon className="w-4 h-4" /><Plus className="w-3 h-3" />Source
            </button>
            <button onClick={() => setShowAddSink(true)} className="px-3 py-2 bg-green-600 text-white rounded shadow hover:bg-green-700 flex items-center gap-2 text-sm">
              <Target className="w-4 h-4" /><Plus className="w-3 h-3" />Sink
            </button>
            {!connected && (
              <span className="px-3 py-2 bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-200 rounded text-sm">Reconnecting to live state...</span>
            )}
            <GraphProfileBar onToast={onToast} />
            <span className="px-3 py-2 bg-white/80 dark:bg-gray-800/80 text-gray-500 dark:text-gray-400 rounded text-xs hidden xl:inline"
              title="Select a node first. Ctrl+Shift+V also keeps the copy's source binding. Cameras cannot be copied (a device opens once); copy a detector or a file source.">
              Ctrl+C / Ctrl+V copy &middot; F2 rename &middot; Del delete node or selected link &middot; drag a link's end away to disconnect
            </span>
          </div>
          <ReactFlow
            nodes={nodes}
            edges={edgesWithActions}
            nodeTypes={nodeTypes}
            edgeTypes={edgeTypes}
            onNodesChange={onNodesChange}
            onConnect={onConnect}
            onReconnectStart={onReconnectStart}
            onReconnect={onReconnect}
            onReconnectEnd={onReconnectEnd}
            edgesReconnectable
            isValidConnection={isValidConnection}
            onNodeClick={(_, node) => { setSelectedId(node.id); setSelectedEdgeId(null); }}
            onEdgeClick={(_, edge) => { setSelectedEdgeId(edge.id); setSelectedId(null); }}
            onPaneClick={() => { setSelectedId(null); setSelectedEdgeId(null); }}
            colorMode={darkMode ? 'dark' : 'light'}
            fitView
          >
            <Background />
            <Controls />
            <MiniMap />
          </ReactFlow>
        </div>
        <BottomStrip snapshot={snapshot} excludeSinkId={inspectorPreviewSinkId} />
      </div>

      {selectedNode && (
        <Inspector
          node={selectedNode}
          onClose={() => setSelectedId(null)}
          onToast={onToast}
          onDeleted={() => setSelectedId(null)}
        />
      )}

      <AddSourceModal isOpen={showAddSource} onClose={() => setShowAddSource(false)} onAdd={handleAddSource}
        cameraSources={(snapshot?.Sources ?? []).filter(source => source.Type === 0).map(source => ({ Id: source.Id, Name: source.Name }))} />
      <AddSinkModal isOpen={showAddSink} onClose={() => setShowAddSink(false)} onAdd={handleAddSink} />
    </div>
  );
};

export const GraphPage: React.FC<{ onToast: (m: string, t: 'success'|'error'|'info') => void; darkMode: boolean }> = (props) => (
  <ReactFlowProvider>
    <GraphPageInner {...props} />
  </ReactFlowProvider>
);
