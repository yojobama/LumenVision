import React, { useEffect, useState } from 'react';
import { Box, Trash2, Upload, Save } from 'lucide-react';
import { ApiService } from '../services/ApiService';
import { ThresholdFields } from '../components/ThresholdFields';
import type { Model } from '../types';

const api = new ApiService();
const VARIANTS = ['YOLOv8', 'YOLOv11'];

const input = 'w-full px-2 py-1 text-sm border border-gray-300 dark:border-gray-600 rounded dark:bg-gray-700 dark:text-white';

// One model: rename it and set the default cutoffs every detector made from it starts with (running detectors follow straight away).
const ModelCard: React.FC<{ model: Model; onChanged: () => void; onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ model, onChanged, onToast }) => {
  const [name, setName] = useState(model.name);
  const [thresholds, setThresholds] = useState({ confThreshold: model.confThreshold, nmsThreshold: model.nmsThreshold });
  const dirty = name !== model.name || thresholds.confThreshold !== model.confThreshold || thresholds.nmsThreshold !== model.nmsThreshold;

  const save = async () => {
    try {
      await api.updateModel(model.id, { name: name.trim(), ...thresholds });
      onToast(`Saved "${name.trim()}"`, 'success');
      onChanged();
    } catch (error) {
      onToast(error instanceof Error && error.message ? error.message : 'Failed to save the model', 'error');
    }
  };

  const remove = async () => {
    if (!confirm(`Delete the model "${model.name}"? Detectors already made from it keep running until restarted.`)) return;
    try {
      await api.deleteModel(model.id);
      onChanged();
    } catch {
      onToast('Failed to delete the model', 'error');
    }
  };

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 space-y-3">
      <div className="flex items-center gap-2">
        <input value={name} onChange={e => setName(e.target.value)} className={input} aria-label="Model name" />
        <button onClick={remove} className="text-red-600 hover:text-red-700" title="Delete"><Trash2 className="w-4 h-4" /></button>
      </div>
      <div className="text-xs text-gray-500 dark:text-gray-400">
        {VARIANTS[model.variant] ?? 'Unknown'} - {model.inputSize}px input - {model.provider === 0 ? 'RKNN (NPU)' : 'ONNX Runtime'}
      </div>
      <ThresholdFields {...thresholds} onChange={setThresholds} />
      <button onClick={save} disabled={!dirty || !name.trim()}
        className="px-3 py-1 bg-blue-600 text-white rounded text-sm hover:bg-blue-700 disabled:opacity-50 flex items-center gap-1">
        <Save className="w-3.5 h-3.5" />Save
      </button>
    </div>
  );
};

// Models are uploaded once here (or from the Add Sink dialog) and then picked by detectors and pipelines.
export const ModelsPage: React.FC<{ onToast: (m: string, t: 'success' | 'error' | 'info') => void }> = ({ onToast }) => {
  const [models, setModels] = useState<Model[] | null>(null);
  const [name, setName] = useState('');
  const [variant, setVariant] = useState(0);
  const [inputSize, setInputSize] = useState(640);
  const [thresholds, setThresholds] = useState({ confThreshold: 0.25, nmsThreshold: 0.45 });
  const [modelFile, setModelFile] = useState<File | null>(null);
  const [labelsFile, setLabelsFile] = useState<File | null>(null);
  const [uploading, setUploading] = useState(false);

  const refresh = () => api.getAllModels().then(setModels).catch(() => setModels([]));

  useEffect(() => {
    refresh();
  }, []);

  const upload = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!modelFile || !name.trim()) return;
    setUploading(true);
    try {
      await api.uploadModel({ name: name.trim(), variant, inputSize, ...thresholds, modelFile, labelsFile: labelsFile ?? undefined });
      onToast(`Model "${name.trim()}" uploaded`, 'success');
      setName('');
      setModelFile(null);
      setLabelsFile(null);
      refresh();
    } catch {
      onToast('Failed to upload the model', 'error');
    } finally {
      setUploading(false);
    }
  };

  return (
    <div className="space-y-6">
      <h1 className="text-2xl font-bold text-gray-900 dark:text-white flex items-center gap-2"><Box className="w-6 h-6" />Models</h1>

      <form onSubmit={upload} className="bg-white dark:bg-gray-800 rounded-lg shadow p-4 space-y-3 max-w-2xl">
        <h2 className="font-semibold text-gray-900 dark:text-white flex items-center gap-2"><Upload className="w-4 h-4" />Upload a model</h2>
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
          <input value={name} onChange={e => setName(e.target.value)} placeholder="Model name" className={input} required />
          <select value={variant} onChange={e => setVariant(parseInt(e.target.value))} className={input}>
            {VARIANTS.map((label, value) => <option key={label} value={value}>{label}</option>)}
          </select>
          <label className="text-xs text-gray-500 dark:text-gray-400">Input size (px)
            <input type="number" min={32} step={32} value={inputSize} onChange={e => setInputSize(parseInt(e.target.value) || 640)} className={input} />
          </label>
        </div>
        <ThresholdFields {...thresholds} onChange={setThresholds} />
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 text-sm text-gray-700 dark:text-gray-300">
          <label>Weights (.onnx or .rknn)
            <input type="file" accept=".onnx,.rknn" onChange={e => setModelFile(e.target.files?.[0] ?? null)} className="block w-full text-sm" />
          </label>
          <label>Class labels (optional, one per line)
            <input type="file" accept=".txt" onChange={e => setLabelsFile(e.target.files?.[0] ?? null)} className="block w-full text-sm" />
          </label>
        </div>
        <p className="text-xs text-gray-400">The backend (ONNX Runtime or RKNN/NPU) follows the file extension. The input size must match what the network was exported with; it cannot be changed afterwards.</p>
        <button type="submit" disabled={uploading || !modelFile || !name.trim()}
          className="px-3 py-1.5 bg-green-600 text-white rounded text-sm hover:bg-green-700 disabled:opacity-50">{uploading ? 'Uploading...' : 'Upload'}</button>
      </form>

      {models !== null && models.length === 0 && <p className="text-gray-500 dark:text-gray-400">No models uploaded yet.</p>}
      <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4">
        {(models ?? []).map(model => <ModelCard key={`${model.id}-${model.name}-${model.confThreshold}-${model.nmsThreshold}`} model={model} onChanged={refresh} onToast={onToast} />)}
      </div>
    </div>
  );
};
