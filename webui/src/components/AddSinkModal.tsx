import React, { useState, useEffect } from 'react';
import { Plus } from 'lucide-react';
import { Modal } from './Modal';
import type { AddSinkOptions, Model } from '../types';
import { ApiService } from '../services/ApiService';

const addSinkModalApi = new ApiService();

export const AddSinkModal: React.FC<{ isOpen: boolean; onClose: () => void; onAdd: (name: string, type: string, options?: AddSinkOptions) => void; }> = ({ isOpen, onClose, onAdd }) => {
  const [name, setName] = useState('');
  const [type, setType] = useState('ApriltagSink');

  // AprilTag fields
  const [tagSize, setTagSize] = useState(0.1651); // meters - 6.5" tags, FRC's usual size
  const [apriltagBackend, setApriltagBackend] = useState(0); // 0 = CPU, 1 = Vulkan (vkapriltag)

  // Object Detection fields
  const [models, setModels] = useState<Model[]>([]);
  const [modelsLoading, setModelsLoading] = useState(false);
  const [modelId, setModelId] = useState<number | ''>('');
  const [uploadingNewModel, setUploadingNewModel] = useState(false);
  const [newModelName, setNewModelName] = useState('');
  const [newModelVariant, setNewModelVariant] = useState(0); // 0 = YOLOv8, 1 = YOLOv11
  const [newModelFile, setNewModelFile] = useState<File | null>(null);
  const [newModelLabelsFile, setNewModelLabelsFile] = useState<File | null>(null);

  const resetForm = () => {
    setName('');
    setType('ApriltagSink');
    setTagSize(0.1651);
    setApriltagBackend(0);
    setModelId('');
    setUploadingNewModel(false);
    setNewModelName('');
    setNewModelVariant(0);
    setNewModelFile(null);
    setNewModelLabelsFile(null);
  };

  // Refresh the model list each time the dialog opens on Object Detection.
  useEffect(() => {
    if (isOpen && type === 'object') {
      setModelsLoading(true);
      addSinkModalApi.getAllModels()
        .then(list => {
          setModels(list);
          if (list.length > 0 && modelId === '') setModelId(list[0].id);
        })
        .catch(() => setModels([]))
        .finally(() => setModelsLoading(false));
    }
  }, [isOpen, type]);

  if (!isOpen) return null;

  const canSubmit = (): boolean => {
    if (!name.trim()) return false;
    if (type === 'ApriltagSink') return tagSize > 0;
    if (type === 'object') {
      if (uploadingNewModel) return !!newModelName.trim() && !!newModelFile;
      return modelId !== '';
    }
    return true;
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!canSubmit()) return;

    let options: AddSinkOptions | undefined;
    if (type === 'ApriltagSink') {
      options = { tagSize, backend: apriltagBackend };
    } else if (type === 'object') {
      options = uploadingNewModel
        ? { newModel: { name: newModelName.trim(), variant: newModelVariant, inputSize: 640, confThreshold: 0.25, nmsThreshold: 0.45, modelFile: newModelFile!, labelsFile: newModelLabelsFile ?? undefined } }
        : { modelId: modelId as number };
    }

    onAdd(name.trim(), type, options);
    resetForm();
    onClose();
  };

  return (
    <Modal isOpen={isOpen} onClose={() => { resetForm(); onClose(); }} title="Add New Sink" wide>
      <form onSubmit={handleSubmit} className="space-y-4">
        <div>
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Sink Name
          </label>
            <input
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
              placeholder="Enter sink name"
              required
            />
        </div>
        <div>
          <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Sink Type
          </label>
          <select
            value={type}
            onChange={(e) => setType(e.target.value)}
            className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
          >
            <option value="ApriltagSink">AprilTag Detection</option>
            {/* Backend (ONNX Runtime vs RKNN) depends on the selected model, not the sink type. */}
            <option value="object">Object Detection</option>
          </select>
        </div>

        {/* AprilTag: tag size and detector backend (Vulkan falls back to CPU without a usable GPU) */}
        {type === 'ApriltagSink' && (
          <div className="flex gap-2 p-3 border border-gray-200 dark:border-gray-700 rounded-md">
            <div className="flex-1">
              <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Tag Size (meters)</label>
              <input type="number" step="any" min="0.001" value={tagSize}
                onChange={(e) => setTagSize(parseFloat(e.target.value) || 0.1651)}
                className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white" />
            </div>
            <div className="flex-1">
              <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Backend</label>
              <select value={apriltagBackend} onChange={(e) => setApriltagBackend(parseInt(e.target.value))}
                className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white">
                <option value={0}>CPU (apriltag)</option>
                <option value={1}>Vulkan (vkapriltag)</option>
              </select>
            </div>
          </div>
        )}

        {/* Object Detection: pick an existing model, or upload a new one */}
        {type === 'object' && (
          <div className="space-y-3 p-3 border border-gray-200 dark:border-gray-700 rounded-md">
            {!uploadingNewModel ? (
              <>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Model</label>
                {modelsLoading ? (
                  <div className="text-sm text-gray-500 dark:text-gray-400">Loading models...</div>
                ) : models.length > 0 ? (
                  <select
                    value={modelId}
                    onChange={(e) => setModelId(parseInt(e.target.value))}
                    className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
                  >
                    {models.map(m => (
                      <option key={m.id} value={m.id}>{m.name} ({m.variant === 1 ? 'YOLOv11' : 'YOLOv8'}, {m.provider === 0 ? 'RKNN/NPU' : 'ONNX'})</option>
                    ))}
                  </select>
                ) : (
                  <div className="text-sm text-gray-500 dark:text-gray-400">No models uploaded yet.</div>
                )}
                <button
                  type="button"
                  onClick={() => setUploadingNewModel(true)}
                  className="text-blue-600 hover:text-blue-700 text-sm flex items-center gap-1"
                >
                  <Plus className="w-3 h-3" />Upload a new model instead
                </button>
              </>
            ) : (
              <>
                <div className="flex items-center justify-between">
                  <label className="block text-sm font-medium text-gray-700 dark:text-gray-300">New Model</label>
                  {models.length > 0 && (
                    <button type="button" onClick={() => setUploadingNewModel(false)} className="text-blue-600 hover:text-blue-700 text-xs">
                      Use an existing model instead
                    </button>
                  )}
                </div>
                <input
                  type="text"
                  value={newModelName}
                  onChange={(e) => setNewModelName(e.target.value)}
                  placeholder="Model name"
                  className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
                />
                <select
                  value={newModelVariant}
                  onChange={(e) => setNewModelVariant(parseInt(e.target.value))}
                  className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
                >
                  <option value={0}>YOLOv8</option>
                  <option value={1}>YOLOv11</option>
                </select>
                <div>
                  <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Model weights (.onnx or .rknn)</label>
                  <input type="file" accept=".onnx,.rknn" onChange={(e) => setNewModelFile(e.target.files?.[0] ?? null)}
                    className="w-full text-sm text-gray-700 dark:text-gray-300" />
                  <p className="text-xs text-gray-400 mt-1">The backend (ONNX Runtime or RKNN/NPU) is picked automatically from the file extension. A .rknn export is produced offline (rknn-toolkit2, on an x86 host) - there's no on-device converter.</p>
                </div>
                <div>
                  <label className="block text-xs text-gray-500 dark:text-gray-400 mb-1">Class labels (optional, one per line)</label>
                  <input type="file" accept=".txt" onChange={(e) => setNewModelLabelsFile(e.target.files?.[0] ?? null)}
                    className="w-full text-sm text-gray-700 dark:text-gray-300" />
                </div>
              </>
            )}
          </div>
        )}


        <div className="flex justify-end space-x-3 mt-6">
          <button type="button" onClick={() => { resetForm(); onClose(); }} className="px-4 py-2 text-gray-600 dark:text-gray-400 hover:text-gray-800 dark:hover:text-gray-200">Cancel</button>
          <button type="submit" disabled={!canSubmit()} className="px-4 py-2 bg-blue-600 text-white rounded hover:bg-blue-700 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2">
            <Plus className="w-4 h-4" />Add Sink
          </button>
        </div>
      </form>
    </Modal>
  );
};
