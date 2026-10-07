import { ApiService } from '../services/ApiService';
import type { PipelineNode } from './model';

const api = new ApiService();

// Shared by the Inspector's buttons and the graph's keyboard shortcuts.

// Asks for confirmation, then deletes the node; resolves to whether it was deleted.
export async function deleteNode(node: PipelineNode): Promise<boolean> {
  if (!confirm(`Delete ${node.data.label}?`)) return false;
  const id = (node.data.raw as { Id: number }).Id;
  if (node.data.kind === 'source') await api.deleteSource(id);
  else await api.deleteSink(id);
  return true;
}

// Copies the node on the server; resolves to the React Flow id of the new node ("source-N" or "sink-N").
export async function duplicateNode(node: PipelineNode, copyBindings: boolean): Promise<string> {
  const id = (node.data.raw as { Id: number }).Id;
  if (node.data.kind === 'source') return `source-${await api.duplicateSource(id)}`;
  return `sink-${await api.duplicateSink(id, copyBindings)}`;
}
