import React, { useState, useEffect } from 'react';
import { Plus, X, Upload, Camera, Video, Image, AlertCircle, CheckCircle } from 'lucide-react';
import { CameraHardwareInfo } from '../types';
import { ApiService } from '../services/ApiService';

interface AddSourceModalProps {
  isOpen: boolean;
  onClose: () => void;
  onAdd: (name: string, type: string, files?: FileList, fps?: number, hardwareInfo?: CameraHardwareInfo, split?: { parentId: number; layout: 'sideBySide' | 'topBottom' }) => void;
  // the cameras already in the graph, to split in two
  cameraSources?: { Id: number; Name: string }[];
}

export const AddSourceModal: React.FC<AddSourceModalProps> = ({ 
  isOpen, 
  onClose, 
  onAdd,
  cameraSources = []
}) => {
  const [name, setName] = useState('');
  const [type, setType] = useState('camera');
  const [files, setFiles] = useState<FileList | null>(null);
  const [fps, setFps] = useState(30);
  const [selectedCamera, setSelectedCamera] = useState<CameraHardwareInfo | null>(null);
  const [cameras, setCameras] = useState<CameraHardwareInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [validationErrors, setValidationErrors] = useState<string[]>([]);
  const [previewFiles, setPreviewFiles] = useState<File[]>([]);
  const [splitParentId, setSplitParentId] = useState<number | null>(null);
  const [splitLayout, setSplitLayout] = useState<'sideBySide' | 'topBottom'>('sideBySide');

  const api = new ApiService();

  useEffect(() => {
    if (isOpen && type === 'camera') {
      loadCameras();
    }
  }, [isOpen, type]);

  // Reset form when modal opens/closes
  useEffect(() => {
    if (!isOpen) {
      resetForm();
    }
  }, [isOpen]);

  const resetForm = () => {
    setName('');
    setType('camera');
    setFiles(null);
    setFps(30);
    setSelectedCamera(null);
    setValidationErrors([]);
    setPreviewFiles([]);
    setSplitParentId(null);
    setSplitLayout('sideBySide');
  };

  const loadCameras = async () => {
    try {
      setLoading(true);
      const cameraList = await api.getCameraHardware();
      setCameras(cameraList);
      if (cameraList.length > 0) {
        setSelectedCamera(cameraList[0]);
      }
    } catch (error) {
      console.error('Failed to load cameras:', error);
      setCameras([]);
    } finally {
      setLoading(false);
    }
  };

  const validateFiles = (fileList: FileList): string[] => {
    const errors: string[] = [];
    const fileArray = Array.from(fileList);
    
    if (fileArray.length === 0) {
      errors.push('No files selected');
      return errors;
    }
    
    if (fileArray.length > 10) {
      errors.push('Maximum 10 files allowed per upload');
      return errors;
    }

    fileArray.forEach((file, index) => {
      if (type === 'video') {
        const validation = api.validateVideoFile(file);
        if (!validation.valid) {
          errors.push(`File ${index + 1} (${file.name}): ${validation.message}`);
        }
      } else if (type === 'image') {
        const validation = api.validateImageFile(file);
        if (!validation.valid) {
          errors.push(`File ${index + 1} (${file.name}): ${validation.message}`);
        }
      }
    });

    return errors;
  };

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const selectedFiles = e.target.files;
    if (selectedFiles && selectedFiles.length > 0) {
      const errors = validateFiles(selectedFiles);
      setValidationErrors(errors);
      
      if (errors.length === 0) {
        setFiles(selectedFiles);
        setPreviewFiles(Array.from(selectedFiles));
        
        // Auto-generate name if not set
        if (!name) {
          if (selectedFiles.length === 1) {
            const fileName = selectedFiles[0].name.split('.')[0];
            setName(fileName);
          } else {
            setName(`${type} Upload (${selectedFiles.length} files)`);
          }
        }
      } else {
        setFiles(null);
        setPreviewFiles([]);
      }
    }
  };

  const removeFile = (indexToRemove: number) => {
    if (files) {
      const dt = new DataTransfer();
      Array.from(files).forEach((file, index) => {
        if (index !== indexToRemove) {
          dt.items.add(file);
        }
      });
      
      const newFiles = dt.files;
      setFiles(newFiles);
      setPreviewFiles(Array.from(newFiles));
      
      if (newFiles.length === 0) {
        setValidationErrors([]);
      } else {
        const errors = validateFiles(newFiles);
        setValidationErrors(errors);
      }
    }
  };

  if (!isOpen) return null;

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (type === 'split') {
      const parentId = splitParentId ?? cameraSources[0]?.Id;
      if (parentId != null) onAdd('', 'split', undefined, undefined, undefined, { parentId, layout: splitLayout });
      onClose();
      return;
    }
    if (!name.trim()) return;
    
    if (validationErrors.length > 0) {
      return;
    }

    if (type === 'camera' && selectedCamera) {
      onAdd(name.trim(), type, undefined, undefined, selectedCamera);
    } else if ((type === 'video' || type === 'image') && files && files.length > 0) {
      onAdd(name.trim(), type, files, type === 'video' ? fps : undefined);
    }
    
    onClose();
  };

  const getTypeIcon = () => {
    switch (type) {
      case 'camera': return <Camera className="w-5 h-5" />;
      case 'video': return <Video className="w-5 h-5" />;
      case 'image': return <Image className="w-5 h-5" />;
      default: return <Plus className="w-5 h-5" />;
    }
  };

  const canSubmit = () => {
    if (type === 'split') return cameraSources.length > 0;
    if (!name.trim()) return false;
    if (validationErrors.length > 0) return false;
    
    if (type === 'camera') {
      return selectedCamera !== null;
    } else if (type === 'video' || type === 'image') {
      return files !== null && files.length > 0;
    }
    
    return false;
  };

  const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  return (
    <div className="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
      <div className="bg-white dark:bg-gray-800 rounded-lg p-6 w-full max-w-2xl mx-4 max-h-[90vh] overflow-y-auto">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-xl font-bold text-gray-900 dark:text-white flex items-center gap-2">
            {getTypeIcon()}
            Add New Source
          </h2>
          <button onClick={onClose} className="text-gray-500 hover:text-gray-700 dark:hover:text-gray-300">
            <X className="w-5 h-5" />
          </button>
        </div>
        
        <form onSubmit={handleSubmit} className="space-y-4">
          {type !== 'split' && (
          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Source Name
            </label>
            <input
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              placeholder="Enter source name"
              required
            />
          </div>
          )}
          
          <div>
            <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Source Type
            </label>
            <select
              value={type}
              onChange={(e) => setType(e.target.value)}
              className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="camera">Camera</option>
              <option value="video">Video Files</option>
              <option value="image">Image Files</option>
              <option value="split">Split a camera (stereo / two views)</option>
            </select>
          </div>

          {/* Splitting a camera */}
          {type === 'split' && (
            <div className="space-y-3">
              {cameraSources.length === 0 ? (
                <div className="text-sm text-red-500 dark:text-red-400">Add a camera source first; a split is cut from one.</div>
              ) : (
                <>
                  <div>
                    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Camera</label>
                    <select
                      value={splitParentId ?? cameraSources[0].Id}
                      onChange={(e) => setSplitParentId(parseInt(e.target.value))}
                      className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                    >
                      {cameraSources.map(camera => <option key={camera.Id} value={camera.Id}>{camera.Name}</option>)}
                    </select>
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Layout</label>
                    <select
                      value={splitLayout}
                      onChange={(e) => setSplitLayout(e.target.value as 'sideBySide' | 'topBottom')}
                      className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                    >
                      <option value="sideBySide">Side by side - left and right halves</option>
                      <option value="topBottom">Top and bottom halves</option>
                    </select>
                  </div>
                  <div className="text-xs text-gray-500 dark:text-gray-400">
                    Adds two sources named after the camera. Bind detectors to them like any camera, or bind them as the left and right eyes of a stereo depth sink.
                    They follow the camera if its resolution changes.
                  </div>
                </>
              )}
            </div>
          )}

          {/* Camera Selection */}
          {type === 'camera' && (
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Camera Device
              </label>
              {loading ? (
                <div className="text-sm text-gray-500 dark:text-gray-400">Loading available cameras...</div>
              ) : cameras.length > 0 ? (
                <select
                  value={selectedCamera?.Path || ''}
                  onChange={(e) => {
                    const camera = cameras.find(c => c.Path === e.target.value);
                    setSelectedCamera(camera || null);
                  }}
                  className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
                  required
                >
                  {cameras.map((camera, index) => (
                    <option key={index} value={camera.Path}>
                      {camera.Name} ({camera.Path})
                    </option>
                  ))}
                </select>
              ) : (
                <div className="text-sm text-red-500 dark:text-red-400">
                  No unregistered cameras found. All available cameras may already be in use.
                </div>
              )}
              <div className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                Only shows cameras not yet registered as sources
              </div>
            </div>
          )}

          {/* File Upload */}
          {(type === 'video' || type === 'image') && (
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                {type === 'video' ? 'Video Files' : 'Image Files'}
              </label>
              <div className="relative">
                <input
                  type="file"
                  onChange={handleFileChange}
                  accept={type === 'video' ? 'video/*' : 'image/*'}
                  multiple
                  className="hidden"
                  id="file-upload"
                  required
                />
                <label
                  htmlFor="file-upload"
                  className={`w-full px-3 py-2 border rounded-md cursor-pointer flex items-center gap-2 transition-colors ${
                    validationErrors.length > 0 
                      ? 'border-red-300 dark:border-red-600 bg-red-50 dark:bg-red-900/20' 
                      : 'border-gray-300 dark:border-gray-600 hover:bg-gray-50 dark:hover:bg-gray-600'
                  } dark:bg-gray-700 dark:text-white`}
                >
                  <Upload className="w-4 h-4" />
                  {files && files.length > 0 
                    ? `${files.length} file(s) selected`
                    : `Choose ${type} files...`
                  }
                </label>
              </div>

              {/* Validation Errors */}
              {validationErrors.length > 0 && (
                <div className="mt-2 p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-md">
                  <div className="flex items-start gap-2">
                    <AlertCircle className="w-4 h-4 text-red-500 mt-0.5 flex-shrink-0" />
                    <div className="text-sm">
                      <div className="font-medium text-red-800 dark:text-red-200 mb-1">File Validation Errors:</div>
                      <ul className="text-red-700 dark:text-red-300 space-y-1">
                        {validationErrors.map((error, index) => (
                          <li key={index}>� {error}</li>
                        ))}
                      </ul>
                    </div>
                  </div>
                </div>
              )}

              {/* File Preview */}
              {previewFiles.length > 0 && validationErrors.length === 0 && (
                <div className="mt-2 p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-md">
                  <div className="flex items-start gap-2 mb-2">
                    <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                    <div className="text-sm font-medium text-green-800 dark:text-green-200">
                      Ready to upload {previewFiles.length} file(s)
                    </div>
                  </div>
                  <div className="space-y-2 max-h-32 overflow-y-auto">
                    {previewFiles.map((file, index) => (
                      <div key={index} className="flex items-center justify-between text-xs text-green-700 dark:text-green-300 bg-green-100 dark:bg-green-900/30 px-2 py-1 rounded">
                        <span className="truncate flex-1">{file.name}</span>
                        <div className="flex items-center gap-2">
                          <span>{formatFileSize(file.size)}</span>
                          <button
                            type="button"
                            onClick={() => removeFile(index)}
                            className="text-red-500 hover:text-red-700"
                          >
                            <X className="w-3 h-3" />
                          </button>
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              )}

              <div className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                {type === 'video' 
                  ? 'Supported formats: MP4, AVI, MOV, WMV, MKV, WebM (max 500MB each)'
                  : 'Supported formats: JPEG, PNG, BMP, GIF, WebP (max 50MB each)'}
                <br />
                Multiple files can be uploaded simultaneously (max 10 files)
              </div>
            </div>
          )}

          {/* FPS for video files */}
          {type === 'video' && (
            <div>
              <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                Frame Rate (FPS)
              </label>
              <input
                type="number"
                value={fps}
                onChange={(e) => setFps(parseInt(e.target.value) || 30)}
                min="1"
                max="120"
                className="w-full px-3 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
              <div className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                Recommended: 30 FPS for most videos. This applies to all uploaded videos.
              </div>
            </div>
          )}
          
          <div className="flex justify-end space-x-3 mt-6">
            <button
              type="button"
              onClick={onClose}
              className="px-4 py-2 text-gray-600 dark:text-gray-400 hover:text-gray-800 dark:hover:text-gray-200 transition-colors"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={!canSubmit()}
              className="px-4 py-2 bg-blue-600 text-white rounded hover:bg-blue-700 disabled:bg-gray-400 disabled:cursor-not-allowed flex items-center gap-2 transition-colors"
            >
              <Plus className="w-4 h-4" />
              Add Source{files && files.length > 1 ? 's' : ''}
            </button>
          </div>
        </form>
      </div>
    </div>
  );
};