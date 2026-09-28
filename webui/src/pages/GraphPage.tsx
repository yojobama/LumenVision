import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ReactFlow, ReactFlowProvider, Background, Controls, MiniMap,
  useNodesState, type Edge, type Connection, type IsValidConnection,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { Plus, Camera as CameraIcon, Target } from 'lucide-react';
import { useStateSocket } from '../hooks/useStateSocket';
import { ApiService } from '../services/ApiService';
import { buildGraph, edgesEqual, type PipelineNode, type PositionStore } from '../graph/model';
import { nodeTypes } from '../graph/PipelineNode';
import { Inspector } from '../graph/Inspector';
import { GraphProfileBar } from '../graph/GraphProfileBar';
import { LeftRail } from '../graph/LeftRail';
import { BottomStrip } from '../graph/BottomStrip';
import { AddSourceModal } from '../components/AddSourceModal';
import { AddSinkModal } from '../components/AddSinkModal';
import type { AddSinkOptions, NodeTypesResponse, NT4Defaults, CameraHardwareInfo } from '../types';

const api = new ApiService();

// The pipeline graph: sources and graph-shaped sinks are nodes, edges are live bindings from
// /ws/state, and dragging a connection performs the bind via REST.
const GraphPageInner: React.FC<{ onToast: (m: string, t: 'success'|'error'|'info') => void; nt4Settings: NT4Defaults; darkMode: boolean }> = ({ onToast, nt4Settings, darkMode }) => {
  const { snapshot, connected } = useStateSocket();
  const [capabilities, setCapabilities] = useState<NodeTypesResponse | null>(null);
  const [nodes, setNodes, onNodesChange] = useNodesState<PipelineNode>([]);
  const [edges, setEdges] = useState<Edge[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [showAddSource, setShowAddSource] = useState(false);
  const [showAddSink, setShowAddSink] = useState(false);
  const positionsRef = useRef(new Map<string, { x: number; y: number }>());

  useEffect(() => {
    api.getNodeTypeCapabilities().then(setCapabilities).catch(() => onToast('Failed to load node capabilities', 'error'));
  }, []);

  // sync the position store with React Flow's drags so the next rebuild keeps them
  useEffect(() => {
    for (const n of nodes) positionsRef.current.set(n.id, n.position);
  }, [nodes]);

  useEffect(() => {
    if (!snapshot) return;
    const store: PositionStore = {
      get: (id) => positionsRef.current.get(id),
      set: (id, pos) => positionsRef.current.set(id, pos),
    };
    const built = buildGraph(snapshot, capabilities, store);
    setNodes(built.nodes);
    // reuse the previous array reference when the edge set is unchanged (see edgesEqual)
    setEdges(prev => (edgesEqual(prev, built.edges) ? prev : built.edges));
  }, [snapshot, capabilities, setNodes]);

  const selectedNode = nodes.find(n => n.id === selectedId) ?? null;
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
          if (targetNode.data.typeName === 'StereoDepthSink') {
            await api.bindStereoDepthSources(targetSinkId, leftId, rightId);
          } else {
            await api.bindStereoSources(targetSinkId, leftId, rightId);
          }
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
    return { x: (count % 4) * 260, y: Math.floor(count / 4) * 140 + 400 };
  };

  const handleAddSource = async (name: string, type: string, files?: FileList, fps?: number, hardwareInfo?: CameraHardwareInfo) => {
    try {
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
      } else if (type === 'calibration') {
        await api.createCameraCalibrationSink(name);
      } else if (type === 'object' && options?.newModel) {
        // upload a new model first, then create the sink from the returned model id; a failed upload throws
        const modelId = await api.uploadModel(options.newModel);
        await api.createObjectDetectionSink(name, modelId);
      } else if (type === 'object' && options?.modelId) {
        await api.createObjectDetectionSink(name, options.modelId);
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
          </div>
          <ReactFlow
            nodes={nodes}
            edges={edges}
            nodeTypes={nodeTypes}
            onNodesChange={onNodesChange}
            onConnect={onConnect}
            isValidConnection={isValidConnection}
            onNodeClick={(_, node) => setSelectedId(node.id)}
            onPaneClick={() => setSelectedId(null)}
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
          nt4Settings={nt4Settings}
        />
      )}

      <AddSourceModal isOpen={showAddSource} onClose={() => setShowAddSource(false)} onAdd={handleAddSource} />
      <AddSinkModal isOpen={showAddSink} onClose={() => setShowAddSink(false)} onAdd={handleAddSink} />
    </div>
  );
};

export const GraphPage: React.FC<{ onToast: (m: string, t: 'success'|'error'|'info') => void; nt4Settings: NT4Defaults; darkMode: boolean }> = (props) => (
  <ReactFlowProvider>
    <GraphPageInner {...props} />
  </ReactFlowProvider>
);
