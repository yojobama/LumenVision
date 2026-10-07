import type { CameraHardwareInfo, CameraMode, CameraControls, Model, StereoCalibrationResult, StereoDepthStats, PipelineProfile, NodeTypesResponse, CameraCalibrationResult, CalibrationCoverage, CalibrationStatus, CalibrationBoard, CalibrationSession, StoredCameraCalibration, StoredStereoCalibration, NetworkTablesStatus, RecordSegment, SnapshotEntry, CameraControl, FrameTransform, ApriltagAdvancedSettings, FieldLayoutInfo, DeviceSettingsData, NetworkStatus, Ipv4Config, PendingNetworkChange } from '../types';
import { apiClient } from '../api/client';

export class ApiService {
  // Relative to the page origin, so it works when the server is reached by hostname or IP.
  private baseUrl = `${window.location.origin}/api`;

  // Source Controller routes
  async getSources(): Promise<any[]> {
    const response = await fetch(`${this.baseUrl}/source/getAll`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async deleteSource(id: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/source/delete?SourceID=${id}`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async renameSource(id: number, name: string): Promise<void> {
    // Note: the route parameter in SourceController is named SinkID
    const response = await fetch(`${this.baseUrl}/source/rename?SourceID=${id}&newName=${encodeURIComponent(name)}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Camera Source Controller routes (/api/cameraSource/*)
  async getCameraHardware(): Promise<CameraHardwareInfo[]> {
    const response = await fetch(`${this.baseUrl}/cameraSource/getNotRegistered`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getRegisteredCameraSources(): Promise<any[]> {
    const response = await fetch(`${this.baseUrl}/cameraSource/getRegistered`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createCameraSource(hardwareInfo: CameraHardwareInfo, name = 'default'): Promise<number> {
    const response = await fetch(`${this.baseUrl}/cameraSource/create?name=${encodeURIComponent(name)}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(hardwareInfo)
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createAllCameraSources(): Promise<void> {
    const response = await fetch(`${this.baseUrl}/cameraSource/createAll`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Resolution/fps and exposure/gain control (CameraSourceController.cs)
  async getCameraModes(id: number): Promise<CameraMode[]> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/modes`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getCameraCurrentMode(id: number): Promise<CameraMode> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/currentMode`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Returns whether the API call succeeded, not whether the device honoured it exactly (the nearest
  // mode may be substituted); re-check getCameraCurrentMode and IsNative.
  async setCameraMode(id: number, mode: CameraMode): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/mode`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(mode)
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // exposureAbsolute is in the backend's native units (V4L2: 100us steps); disable auto-exposure
  // first, as most UVC hardware ignores manual exposure otherwise.
  async setCameraExposure(id: number, exposureAbsolute: number): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/exposure?exposureAbsolute=${exposureAbsolute}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async setCameraAutoExposure(id: number, enabled: boolean): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/autoExposure?enabled=${enabled}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async setCameraGain(id: number, gain: number): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/gain?gain=${gain}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // the device's own exposure/gain ranges and current values - see CameraControlRange
  async getCameraControls(id: number): Promise<CameraControls> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/controls`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Splits one camera source's frame into a fixed crop published as an independent source (used
  // for side-by-side/top-bottom stereo).
  async createRoiSource(id: number, x: number, y: number, width: number, height: number): Promise<number> {
    const { data, error } = await apiClient.POST('/cameraSource/{id}/roi', {
      params: { path: { id }, query: { x, y, width, height } },
    });
    if (error || data == null) throw new Error('Failed to create ROI source');
    return data;
  }

  // Reports a stale/missing calibration after a resolution change (shown as a warning badge by
  // Inspector/PipelineNode). Uses the OpenAPI-generated typed client (src/api/client.ts).
  async getCalibrationStatus(id: number): Promise<CalibrationStatus> {
    const { data, error } = await apiClient.GET('/cameraSource/{id}/calibrationStatus', {
      params: { path: { id } },
    });
    if (error) throw new Error('Failed to fetch calibration status');
    // cast: the generated schema models nullable int? as optional, but the server serialises null
    return data as CalibrationStatus;
  }

  // GET: every camera calibration saved to disk (CalibrationManager.cs)
  async getSavedCalibrations(): Promise<StoredCameraCalibration[]> {
    const response = await fetch(`${this.baseUrl}/calibration/saved`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // GET: every stereo calibration saved to disk (StereoCalibrationManager.cs)
  async getSavedStereoCalibrations(): Promise<StoredStereoCalibration[]> {
    const response = await fetch(`${this.baseUrl}/calibration/savedStereo`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Calibration sessions (CalibrationController.cs): interactive runs that are never graph nodes.
  private boardParams(board: CalibrationBoard): URLSearchParams {
    const params = new URLSearchParams({
      boardType: String(board.boardType), rows: String(board.rows), cols: String(board.cols),
      squareSizeMeters: String(board.squareSizeMeters),
    });
    if (board.markerSizeMeters != null) params.set('markerSizeMeters', String(board.markerSizeMeters));
    return params;
  }

  private async sessionRequest<T>(path: string, method = 'GET'): Promise<T> {
    const response = await fetch(`${this.baseUrl}/calibration/${path}`, { method });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async startCameraCalibration(sourceId: number, board: CalibrationBoard): Promise<CalibrationSession> {
    const params = this.boardParams(board);
    params.set('sourceId', String(sourceId));
    return this.sessionRequest(`camera/start?${params}`, 'POST');
  }

  // ChArUco is unsupported for stereo (StereoCalibrator.h); the board is always a checkerboard
  async startStereoCalibration(leftSourceId: number, rightSourceId: number, board: CalibrationBoard): Promise<CalibrationSession> {
    const params = this.boardParams({ ...board, boardType: 0 });
    params.set('leftSourceId', String(leftSourceId));
    params.set('rightSourceId', String(rightSourceId));
    return this.sessionRequest(`stereo/start?${params}`, 'POST');
  }

  // one side-by-side stereo camera, split in half into left/right eyes by the server
  async startStereoSplitCalibration(sourceId: number, board: CalibrationBoard): Promise<CalibrationSession> {
    const params = this.boardParams({ ...board, boardType: 0 });
    params.set('sourceId', String(sourceId));
    return this.sessionRequest(`stereo/startSplit?${params}`, 'POST');
  }

  getCalibrationSessions(): Promise<CalibrationSession[]> {
    return this.sessionRequest('sessions');
  }

  getCalibrationSession(id: number): Promise<CalibrationSession> {
    return this.sessionRequest(`${id}`);
  }

  async stopCalibrationSession(id: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/calibration/${id}/stop`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // saves the latest detected board (camera) or matched pair (stereo); false if none is available
  saveCalibrationDetection(id: number): Promise<boolean> {
    return this.sessionRequest(`${id}/saveDetection`, 'POST');
  }

  // number of saved snapshots (camera) or pairs (stereo)
  getCalibrationCount(id: number): Promise<number> {
    return this.sessionRequest(`${id}/count`);
  }

  async clearCalibrationEntries(id: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/calibration/${id}/entries`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  runCameraCalibration(id: number): Promise<CameraCalibrationResult> {
    return this.sessionRequest(`${id}/runCamera`, 'POST');
  }

  // gate real use on the returned EpipolarRms (< 0.5px), as StereoRms alone does not predict validity
  runStereoCalibration(id: number): Promise<StereoCalibrationResult> {
    return this.sessionRequest(`${id}/runStereo`, 'POST');
  }

  // data for the coverage heatmap; `eye` applies only to stereo sessions
  getCalibrationCoverage(id: number, eye?: 'left' | 'right'): Promise<CalibrationCoverage> {
    return this.sessionRequest(`${id}/coverage${eye ? `?eye=${eye}` : ''}`);
  }

  // GET: the server's recent diagnostic log lines (LogController.cs)
  async getLogTail(lines: number = 200): Promise<string[]> {
    const { data, error } = await apiClient.GET('/log/tail', { params: { query: { lines } } });
    if (error) throw new Error('Failed to fetch server log');
    return data ?? [];
  }

  // Graph Profile Controller routes (/api/graphProfile/*): save/restore the whole node graph by name
  async listGraphProfiles(): Promise<string[]> {
    const { data, error } = await apiClient.GET('/graphProfile/list');
    if (error) throw new Error('Failed to list graph profiles');
    return data ?? [];
  }

  async saveGraphProfileAs(name: string): Promise<void> {
    const { error } = await apiClient.POST('/graphProfile/saveCurrentAs', { params: { query: { name } } });
    if (error) throw new Error('Failed to save graph profile');
  }

  // Tears down the live graph and rebuilds the named one; irreversible unless the current graph was saved.
  async activateGraphProfile(name: string): Promise<void> {
    const { error } = await apiClient.POST('/graphProfile/activate', { params: { query: { name } } });
    if (error) throw new Error('Failed to activate graph profile');
  }

  // Video File Source Controller routes (/api/videoFileSource/*)
  async getAllVideoFileSources(): Promise<any[]> {
    const response = await fetch(`${this.baseUrl}/videoFileSource/getAll`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createVideoFileSource(files: FileList, fps: number = 30): Promise<number[]> {
    const formData = new FormData();
    Array.from(files).forEach(file => {
      formData.append('files', file);
    });
    
    const response = await fetch(`${this.baseUrl}/videoFileSource/create`, {
      method: 'POST',
      body: formData
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async changeVideoFileFPS(fps: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/videoFileSource/changeFPS?fps=${fps}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Image File Source Controller routes (/api/imageFileSource/*)
  async getAllImageFileSources(): Promise<any[]> {
    const response = await fetch(`${this.baseUrl}/imageFileSource/get`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createImageFileSource(files: FileList): Promise<number[]> {
    const formData = new FormData();
    Array.from(files).forEach(file => {
      formData.append('files', file);
    });
    
    const response = await fetch(`${this.baseUrl}/imageFileSource/create`, {
      method: 'POST',
      body: formData
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Sink Controller routes
  async bindSinkToSource(sinkId: number, sourceId: number): Promise<void> {
      const response = await fetch(`${this.baseUrl}/sink/bind?SinkID=${sinkId}&SourceID=${sourceId}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async unbindSinkFromSource(sinkId: number, sourceId?: number): Promise<void> {
    const url = sourceId 
      ? `${this.baseUrl}/sink/unbind?SinkID=${sinkId}&SourceID=${sourceId}`
        : `${this.baseUrl}/sink/unbind?SinkID=${sinkId}`;
    const response = await fetch(url, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async deleteSink(id: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/sink/delete?SinkID=${id}`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // AprilTag Sink Controller routes
  async createApriltagSink(name: string, type: string): Promise<number> {
      const response = await fetch(`${this.baseUrl}/apriltagSink/create?name=${encodeURIComponent(name)}&type=apriltag`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Create an AprilTag sink with an explicit CPU/Vulkan backend (0/1) and tag size, without calibration data.
  async createApriltagSinkWithBackend(name: string, tagSize: number, backend: number, frameWidth = 0, frameHeight = 0): Promise<number> {
    const params = new URLSearchParams({ name, tagSize: String(tagSize), backend: String(backend), frameWidth: String(frameWidth), frameHeight: String(frameHeight) });
    const response = await fetch(`${this.baseUrl}/apriltagSink/createWithBackend?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Backend a sink is actually running; Vulkan falls back to CPU if no usable device is found.
  async getApriltagBackendName(sinkId: number): Promise<string> {
    const response = await fetch(`${this.baseUrl}/apriltagSink/backend?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Same as getApriltagBackendName, as the 0/1 enum rather than a display string.
  async getApriltagBackendKind(sinkId: number): Promise<number> {
    const response = await fetch(`${this.baseUrl}/apriltagSink/backendKind?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Switches an existing sink between CPU/Vulkan in place, preserving id/tag size/calibration/bindings.
  // Omitted nthreads/quadDecimate/refineEdges/refineMode carry forward the sink's current tuning.
  async setApriltagBackend(sinkId: number, backend: number, nthreads?: number, quadDecimate?: number, refineEdges?: boolean, refineMode?: number,
    advanced?: Partial<ApriltagAdvancedSettings>): Promise<void> {
    let url = `${this.baseUrl}/apriltagSink/backend?sinkId=${sinkId}&backend=${backend}`;
    if (advanced) {
      for (const [key, value] of Object.entries(advanced)) if (value !== undefined) url += `&${key}=${value}`;
    }
    if (nthreads !== undefined) url += `&nthreads=${nthreads}`;
    if (quadDecimate !== undefined) url += `&quadDecimate=${quadDecimate}`;
    if (refineEdges !== undefined) url += `&refineEdges=${refineEdges}`;
    if (refineMode !== undefined) url += `&refineMode=${refineMode}`;
    const response = await fetch(url, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Current threads/quad_decimate/refine_edges in effect; on Vulkan quadDecimate is the integer the
  // GPU pipeline actually runs.
  async getApriltagTuning(sinkId: number): Promise<{ threads: number; quadDecimate: number; quadDecimateSupported: boolean; refineEdges: boolean; refineMode: number; refineModeSupported: boolean;
    advanced: ApriltagAdvancedSettings; quadSigmaSupported: boolean }> {
    const response = await fetch(`${this.baseUrl}/apriltagSink/tuning?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const dto = await response.json();
    return {
      threads: dto.Threads, quadDecimate: dto.QuadDecimate, quadDecimateSupported: dto.QuadDecimateSupported, refineEdges: dto.RefineEdges,
      refineMode: dto.RefineMode, refineModeSupported: dto.RefineModeSupported, quadSigmaSupported: dto.QuadSigmaSupported,
      advanced: {
        family: dto.Family, quadSigma: dto.QuadSigma, maxHamming: dto.MaxHamming, decisionMargin: dto.DecisionMargin,
        poseIterations: dto.PoseIterations, multiTag: dto.MultiTag, singleTagPose: dto.SingleTagPose,
      },
    };
  }

  // Object Detection Sink Controller routes
  async createObjectDetectionSink(name: string, modelId: number, thresholds?: { confThreshold: number; nmsThreshold: number }): Promise<number> {
    let url = `${this.baseUrl}/objectDetectionSink/create?name=${encodeURIComponent(name)}&modelId=${modelId}`;
    if (thresholds) url += `&confThreshold=${thresholds.confThreshold}&nmsThreshold=${thresholds.nmsThreshold}`;
    const response = await fetch(url, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Backend an existing sink runs (ONNX Runtime vs RKNN), fixed by the model's file format; read-only.
  async getObjectDetectionSinkBackend(sinkId: number): Promise<string> {
    const response = await fetch(`${this.baseUrl}/objectDetectionSink/backend?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Model Controller routes (uploaded YOLOv8/YOLOv11 models, ONNX or RKNN)
  // The server answers in PascalCase like every other route; the Model type is lower-case, so the fields are mapped here.
  async getAllModels(): Promise<Model[]> {
    const response = await fetch(`${this.baseUrl}/model/getAll`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const raw: Array<Record<string, unknown>> = await response.json();
    return raw.map(m => ({
      id: (m.Id ?? m.id) as number,
      name: (m.Name ?? m.name) as string,
      variant: (m.Variant ?? m.variant) as number,
      inputSize: (m.InputSize ?? m.inputSize) as number,
      confThreshold: (m.ConfThreshold ?? m.confThreshold) as number,
      nmsThreshold: (m.NmsThreshold ?? m.nmsThreshold) as number,
      provider: (m.Provider ?? m.provider) as number,
    }));
  }

  // Renames a model and/or changes its default cutoffs (0.01-1); detectors running on it are retuned. The input size cannot change.
  async updateModel(id: number, changes: { name?: string; confThreshold?: number; nmsThreshold?: number }): Promise<void> {
    const params = new URLSearchParams({ id: String(id) });
    if (changes.name !== undefined) params.set('name', changes.name);
    if (changes.confThreshold !== undefined) params.set('confThreshold', String(changes.confThreshold));
    if (changes.nmsThreshold !== undefined) params.set('nmsThreshold', String(changes.nmsThreshold));
    const response = await fetch(`${this.baseUrl}/model/update?${params}`, { method: 'PATCH' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
  }

  async getObjectDetectionThresholds(sinkId: number): Promise<{ confThreshold: number; nmsThreshold: number; modelId: number | null }> {
    const response = await fetch(`${this.baseUrl}/objectDetectionSink/thresholds?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const dto = await response.json();
    return { confThreshold: dto.ConfThreshold, nmsThreshold: dto.NmsThreshold, modelId: dto.ModelId };
  }

  async setObjectDetectionThresholds(sinkId: number, confThreshold: number, nmsThreshold: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/objectDetectionSink/thresholds?sinkId=${sinkId}&confThreshold=${confThreshold}&nmsThreshold=${nmsThreshold}`, { method: 'PATCH' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
  }

  async uploadModel(params: {
    name: string;
    variant: number; // 0 = YOLOv8, 1 = YOLOv11
    inputSize?: number;
    confThreshold?: number;
    nmsThreshold?: number;
    modelFile: File;
    labelsFile?: File;
  }): Promise<number> {
    const formData = new FormData();
    formData.append('name', params.name);
    formData.append('variant', String(params.variant));
    formData.append('inputSize', String(params.inputSize ?? 640));
    formData.append('confThreshold', String(params.confThreshold ?? 0.25));
    formData.append('nmsThreshold', String(params.nmsThreshold ?? 0.45));
    formData.append('model', params.modelFile);
    if (params.labelsFile) formData.append('labels', params.labelsFile);

    const response = await fetch(`${this.baseUrl}/model/upload`, { method: 'POST', body: formData });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async deleteModel(id: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/model/delete?id=${id}`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // NetworkTables Sink Controller routes
  async createNetworkTablesSinkForTeam(name: string, teamNumber: number, rootTable = 'lumenvision', clientIdentity = 'lumenvision'): Promise<number> {
    const params = new URLSearchParams({ name, teamNumber: String(teamNumber), rootTable, clientIdentity });
    const response = await fetch(`${this.baseUrl}/networkTablesSink/createForTeam?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createNetworkTablesSinkForServer(name: string, serverAddress: string, port = 0, rootTable = 'lumenvision', clientIdentity = 'lumenvision'): Promise<number> {
    const params = new URLSearchParams({ name, serverAddress, port: String(port), rootTable, clientIdentity });
    const response = await fetch(`${this.baseUrl}/networkTablesSink/createForServer?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getNetworkTablesStatus(sinkId: number): Promise<NetworkTablesStatus> {
    const response = await fetch(`${this.baseUrl}/networkTablesSink/status?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // WebRTC Sink Controller routes
  async createWebRTCSink(name: string, bitrateKbps = 4000, fps = 30, encoderName = 'libx264'): Promise<number> {
    const params = new URLSearchParams({ name, bitrateKbps: String(bitrateKbps), fps: String(fps), encoderName });
    const response = await fetch(`${this.baseUrl}/webrtcSink/create?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Blocks briefly for server-side ICE gathering. Returned as raw text/plain because SDP contains
  // literal \r\n that breaks response.json().
  async getWebRTCOffer(sinkId: number): Promise<string> {
    const response = await fetch(`${this.baseUrl}/webrtcSink/offer?sinkId=${sinkId}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.text();
  }

  async sendWebRTCAnswer(sinkId: number, sdp: string): Promise<void> {
    const response = await fetch(`${this.baseUrl}/webrtcSink/answer?sinkId=${sinkId}`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: sdp
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async sendWebRTCIceCandidate(sinkId: number, candidate: string, mid: string): Promise<void> {
    const params = new URLSearchParams({ sinkId: String(sinkId), candidate, mid: mid ?? '' });
    const response = await fetch(`${this.baseUrl}/webrtcSink/candidate?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async getWebRTCSinkStatus(sinkId: number): Promise<{ Connected: boolean; IceState: number; GatheringComplete: boolean }> {
    const response = await fetch(`${this.baseUrl}/webrtcSink/status?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // MJPEG Sink Controller routes; used only as a fallback when a WebRTC preview fails. Bind/toggle
  // via PATCH /sink/bind and /sink/toggle.
  async createMjpegSink(name: string, jpegQuality = 80): Promise<number> {
    const params = new URLSearchParams({ name, jpegQuality: String(jpegQuality) });
    const response = await fetch(`${this.baseUrl}/mjpegSink/create?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // mounted at "/stream/mjpeg" (not under "/api"): a raw multipart HTTP response, so it does not use baseUrl
  getMjpegStreamUrl(sinkId: number): string {
    return `${window.location.origin}/stream/mjpeg?SinkID=${sinkId}`;
  }

  // Record Sink Controller routes: segmented MP4 recording with a JSON-Lines telemetry sidecar per
  // segment. Undefined dstFolder/encoderName use server defaults.
  // Start/stop recording on every source at once (SinkManager.SetAllRecording).
  // Returns how many RecordSinks are running.
  async setAllRecording(enabled: boolean): Promise<number> {
    const response = await fetch(`${this.baseUrl}/recordSink/all?enabled=${enabled}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createRecordSink(name: string, options?: { dstFolder?: string; encoderName?: string; bitrateKbps?: number; fps?: number; segmentSeconds?: number; maxFolderSizeBytes?: number; maxFileCount?: number }): Promise<number> {
    const params = new URLSearchParams({ name });
    if (options?.dstFolder) params.set('dstFolder', options.dstFolder);
    if (options?.encoderName) params.set('encoderName', options.encoderName);
    if (options?.bitrateKbps != null) params.set('bitrateKbps', String(options.bitrateKbps));
    if (options?.fps != null) params.set('fps', String(options.fps));
    if (options?.segmentSeconds != null) params.set('segmentSeconds', String(options.segmentSeconds));
    if (options?.maxFolderSizeBytes != null) params.set('maxFolderSizeBytes', String(options.maxFolderSizeBytes));
    if (options?.maxFileCount != null) params.set('maxFileCount', String(options.maxFileCount));
    const response = await fetch(`${this.baseUrl}/recordSink/create?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getRecordSinkSegments(sinkId: number): Promise<RecordSegment[]> {
    const response = await fetch(`${this.baseUrl}/recordSink/${sinkId}/segments`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // opened directly by the browser (<a href>/window.open); the response is a file download, not JSON
  getRecordSinkDownloadUrl(sinkId: number, fileName: string): string {
    return `${this.baseUrl}/recordSink/${sinkId}/download?${new URLSearchParams({ file: fileName })}`;
  }

  // use an already-recorded segment as a VideoFileSource without re-uploading
  async promoteRecordSinkSegment(sinkId: number, fileName: string, name?: string): Promise<number> {
    const params = new URLSearchParams({ file: fileName });
    if (name) params.set('name', name);
    const response = await fetch(`${this.baseUrl}/recordSink/${sinkId}/promote?${params.toString()}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async deleteRecordSinkSegment(sinkId: number, fileName: string): Promise<boolean> {
    const params = new URLSearchParams({ file: fileName });
    const response = await fetch(`${this.baseUrl}/recordSink/${sinkId}/segments?${params.toString()}`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async bindStereoDepthSources(sinkId: number, leftSourceId: number, rightSourceId: number): Promise<void> {
    const params = new URLSearchParams({ leftSourceId: String(leftSourceId), rightSourceId: String(rightSourceId) });
    const response = await fetch(`${this.baseUrl}/stereoDepthSink/${sinkId}/bind?${params.toString()}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Stereo Depth Sink Controller routes
  async createStereoDepthSink(params: {
    name: string; backend: number; minDepthMeters: number; maxDepthMeters: number;
    maxSkewUs: number; frameOutput: number; calibration: StereoCalibrationResult;
  }): Promise<number> {
    const query = new URLSearchParams({
      name: params.name, backend: String(params.backend),
      minDepthMeters: String(params.minDepthMeters), maxDepthMeters: String(params.maxDepthMeters),
      maxSkewUs: String(params.maxSkewUs), frameOutput: String(params.frameOutput),
    });
    const response = await fetch(`${this.baseUrl}/stereoDepthSink/create?${query.toString()}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(params.calibration),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getStereoDepthBackendName(sinkId: number): Promise<string> {
    const response = await fetch(`${this.baseUrl}/stereoDepthSink/${sinkId}/backendName`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getStereoDepthStats(sinkId: number): Promise<StereoDepthStats> {
    const response = await fetch(`${this.baseUrl}/stereoDepthSink/${sinkId}/stats`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Depth Fusion Sink Controller routes: fuses a detector's boxes with a StereoDepthSink's depth grid.
  // Bind the detector (to the depth sink's rectified-left output) with bindSinkToSource, then attach the depth source.
  async createDepthFusionSink(name: string): Promise<number> {
    const response = await fetch(`${this.baseUrl}/depthFusionSink/create?name=${encodeURIComponent(name)}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async attachDepthFusionSource(fusionSinkId: number, stereoDepthSinkId: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/depthFusionSink/${fusionSinkId}/attachDepthSource?stereoDepthSinkId=${stereoDepthSinkId}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Device Controller routes
  async getDeviceCPUUsage(): Promise<number> {
    const response = await fetch(`${this.baseUrl}/device/cpuUsage`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getDeviceRAMUsage(): Promise<number> {
    const response = await fetch(`${this.baseUrl}/device/ramUsage`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getDeviceDiskUsage(): Promise<number> {
    const response = await fetch(`${this.baseUrl}/device/diskUsage`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Data aggregation helpers
  async getAllSources(): Promise<any[]> {
    try {
      // Get sources from all specific endpoints
      const [mainSources, cameras, videos, images] = await Promise.allSettled([
        this.getSources(),
        this.getRegisteredCameraSources(),
        this.getAllVideoFileSources(),
        this.getAllImageFileSources()
      ]);
      
      let allSources: any[] = [];
      
      // Add main sources if available
      if (mainSources.status === 'fulfilled') {
        allSources = [...allSources, ...mainSources.value];
      }
      
      // Add camera sources
      if (cameras.status === 'fulfilled') {
        allSources = [...allSources, ...cameras.value];
      }
      
      // Add video file sources
      if (videos.status === 'fulfilled') {
        allSources = [...allSources, ...videos.value];
      }
      
      // Add image file sources
      if (images.status === 'fulfilled') {
        allSources = [...allSources, ...images.value];
      }
      
      // Remove duplicates based on ID
      const uniqueSources = allSources.filter((source, index, self) => 
        index === self.findIndex(s => (s.Id || s.id) === (source.Id || source.id))
      );
      
      return uniqueSources;
    } catch (error) {
      console.error('Failed to load sources:', error);
      return [];
    }
  }

  async getAllSinks(): Promise<any[]> {
      const response = await fetch(`${this.baseUrl}/sink/getAll`, { method: 'GET' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return response.json();
  }

  // File upload methods
  async uploadVideoFiles(files: FileList, fps: number = 30): Promise<{
    success: boolean;
    sourceIds: number[];
    message: string;
  }> {
    try {
      const sourceIds = await this.createVideoFileSource(files, fps);
      return {
        success: true,
        sourceIds,
        message: `Successfully uploaded ${files.length} video file(s) with IDs: ${sourceIds.join(', ')}`
      };
    } catch (error) {
      return {
        success: false,
        sourceIds: [],
        message: `Failed to upload video files: ${error}`
      };
    }
  }

  async uploadImageFiles(files: FileList): Promise<{
    success: boolean;
    sourceIds: number[];
    message: string;
  }> {
    try {
      const sourceIds = await this.createImageFileSource(files);
      return {
        success: true,
        sourceIds,
        message: `Successfully uploaded ${files.length} image file(s) with IDs: ${sourceIds.join(', ')}`
      };
    } catch (error) {
      return {
        success: false,
        sourceIds: [],
        message: `Failed to upload image files: ${error}`
      };
    }
  }

  // File validation
  validateVideoFile(file: File): { valid: boolean; message: string } {
    const validVideoTypes = ['video/mp4', 'video/avi', 'video/mov', 'video/wmv', 'video/mkv', 'video/webm'];
    const maxSize = 500 * 1024 * 1024; // 500MB
    
    if (!validVideoTypes.includes(file.type)) {
      return {
        valid: false,
        message: `Invalid video format. Supported formats: ${validVideoTypes.join(', ')}`
      };
    }
    
    if (file.size > maxSize) {
      return {
        valid: false,
        message: `File too large. Maximum size: ${maxSize / 1024 / 1024}MB`
      };
    }
    
    return { valid: true, message: 'Valid video file' };
  }

  validateImageFile(file: File): { valid: boolean; message: string } {
    const validImageTypes = ['image/jpeg', 'image/jpg', 'image/png', 'image/bmp', 'image/gif', 'image/webp'];
    const maxSize = 50 * 1024 * 1024; // 50MB
    
    if (!validImageTypes.includes(file.type)) {
      return {
        valid: false,
        message: `Invalid image format. Supported formats: ${validImageTypes.join(', ')}`
      };
    }
    
    if (file.size > maxSize) {
      return {
        valid: false,
        message: `File too large. Maximum size: ${maxSize / 1024 / 1024}MB`
      };
    }
    
    return { valid: true, message: 'Valid image file' };
  }

  // Compatibility methods
  async getSource(id: number): Promise<any> {
    const sources = await this.getAllSources();
    return sources.find(s => (s.Id || s.id) === id) || null;
  }

  async getSinks(): Promise<any[]> {
    return this.getAllSinks();
  }

  async getSink(id: number): Promise<any> {
    const sinks = await this.getAllSinks();
    return sinks.find(s => (s.Id || s.id) === id) || null;
  }

  async addSink(name: string, type: string): Promise<number> {
    return this.createApriltagSink(name, type);
  }

  async changeSourceName(id: number, name: string): Promise<void> {
    return this.renameSource(id, name);
  }

  async renameSink(id: number, name: string): Promise<void> {
        const response = await fetch(`${this.baseUrl}/sink/rename?NewName=${encodeURIComponent(name)}&SinkID=${encodeURIComponent(id)}`, { method: 'PATCH' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // the sink's latest result as JSON (Manager::GetSinkResult); its shape varies per sink type, and the
  // controller writes the body directly because the default serialiser mis-escapes quotes
  async getSinkResult(sinkId: number): Promise<unknown> {
    const response = await fetch(`${this.baseUrl}/sink/getResult?SinkID=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getSinkStatus(sinkId: number): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/sink/getStatus?SinkID=${sinkId}`, { method: 'GET' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async toggleSink(sinkId: number, enabled: boolean): Promise<void> {
    const response = await fetch(`${this.baseUrl}/sink/toggle?SinkID=${sinkId}&Enabled=${enabled}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async enableSink(id: number): Promise<void> {
      const response = await fetch(`${this.baseUrl}/sink/toggle?SinkID=${encodeURIComponent(id)}&Enabled=${true}`, { method: 'PATCH' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async disableSink(id: number): Promise<void> {
      const response = await fetch(`${this.baseUrl}/sink/toggle?SinkID=${encodeURIComponent(id)}&Enabled=${false}`, { method: 'PATCH' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Capabilities Controller routes (/api/capabilities/*)
  async getNodeTypeCapabilities(): Promise<NodeTypesResponse> {
    const response = await fetch(`${this.baseUrl}/capabilities/nodeTypes`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getEnabledFeatures(): Promise<string[]> {
    const response = await fetch(`${this.baseUrl}/capabilities/features`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Pipeline Profile Controller routes (/api/source/profiles*): a profile belongs to a camera source;
  // activating one rebuilds the bound detection sink (PipelineProfileController.cs).
  async getProfiles(sourceId: number): Promise<PipelineProfile[]> {
    const response = await fetch(`${this.baseUrl}/source/profiles?sourceId=${sourceId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createApriltagProfile(sourceId: number, name: string, tagSize: number, options?: {
    backend?: number; frameWidth?: number; frameHeight?: number; driverMode?: boolean; refineMode?: number;
  }): Promise<number> {
    const params = new URLSearchParams({ sourceId: String(sourceId), name, tagSize: String(tagSize) });
    if (options?.backend != null) params.set('backend', String(options.backend));
    if (options?.frameWidth != null) params.set('frameWidth', String(options.frameWidth));
    if (options?.frameHeight != null) params.set('frameHeight', String(options.frameHeight));
    if (options?.driverMode != null) params.set('driverMode', String(options.driverMode));
    if (options?.refineMode != null) params.set('refineMode', String(options.refineMode));
    const response = await fetch(`${this.baseUrl}/source/profiles/apriltag?${params}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async createObjectDetectionProfile(sourceId: number, name: string, modelId: number, thresholds?: { confThreshold: number; nmsThreshold: number }): Promise<number> {
    const params = new URLSearchParams({ sourceId: String(sourceId), name, modelId: String(modelId) });
    if (thresholds) {
      params.set('confThreshold', String(thresholds.confThreshold));
      params.set('nmsThreshold', String(thresholds.nmsThreshold));
    }
    const response = await fetch(`${this.baseUrl}/source/profiles/objectDetection?${params}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async activateProfile(sourceId: number, index: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/source/profiles/activate?sourceId=${sourceId}&index=${index}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Gives a pipeline its own camera settings (starting from what the camera does now) or takes them away again
  async setProfileCameraOverrides(sourceId: number, index: number, enabled: boolean): Promise<void> {
    const response = await fetch(`${this.baseUrl}/source/profiles/cameraOverrides?sourceId=${sourceId}&index=${index}&enabled=${enabled}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async deleteProfile(sourceId: number, index: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/source/profiles?sourceId=${sourceId}&index=${index}`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async getCameraControlList(id: number): Promise<CameraControl[]> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/controlList`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Resolves to whether the device accepted the value
  async setCameraControl(id: number, controlId: number, value: number): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/control?controlId=${controlId}&value=${value}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getCameraTransform(id: number): Promise<FrameTransform> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/transform`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Rejects with the server's explanation when the transform is invalid
  async setCameraTransform(id: number, transform: FrameTransform): Promise<void> {
    const response = await fetch(`${this.baseUrl}/cameraSource/${id}/transform`, {
      method: 'PATCH', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(transform),
    });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
  }

  async setWebRTCSettings(sinkId: number, bitrateKbps: number, fps: number, scaleDivisor: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/webrtcSink/settings?sinkId=${sinkId}&bitrateKbps=${bitrateKbps}&fps=${fps}&scaleDivisor=${scaleDivisor}`, { method: 'PATCH' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
  }

  async setMjpegSettings(sinkId: number, jpegQuality: number, scaleDivisor: number): Promise<void> {
    const response = await fetch(`${this.baseUrl}/mjpegSink/settings?sinkId=${sinkId}&jpegQuality=${jpegQuality}&scaleDivisor=${scaleDivisor}`, { method: 'PATCH' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
  }

  // Field layouts: the ones shipped with the server, and putting one (or an uploaded WPILib layout JSON) onto a detector or pipeline.
  // Each resolves to the number of tags loaded, or -1 when the layout was not valid.
  async listFieldLayouts(): Promise<FieldLayoutInfo[]> {
    const response = await fetch(`${this.baseUrl}/fieldLayouts`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async getSinkFieldLayoutTagCount(sinkId: number): Promise<number> {
    const response = await fetch(`${this.baseUrl}/apriltagSink/fieldLayoutTagCount?sinkId=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async setSinkBundledFieldLayout(sinkId: number, layout: string): Promise<number> {
    const response = await fetch(`${this.baseUrl}/apriltagSink/fieldLayoutBundled?sinkId=${sinkId}&layout=${encodeURIComponent(layout)}`, { method: 'POST' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  async uploadSinkFieldLayout(sinkId: number, json: string): Promise<number> {
    const response = await fetch(`${this.baseUrl}/apriltagSink/fieldLayout?sinkId=${sinkId}`, { method: 'POST', body: json });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  async getProfileFieldLayoutTagCount(sourceId: number, index: number): Promise<number> {
    const response = await fetch(`${this.baseUrl}/source/profiles/fieldLayoutTagCount?sourceId=${sourceId}&index=${index}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async setProfileBundledFieldLayout(sourceId: number, index: number, layout: string): Promise<number> {
    const response = await fetch(`${this.baseUrl}/source/profiles/fieldLayoutBundled?sourceId=${sourceId}&index=${index}&layout=${encodeURIComponent(layout)}`, { method: 'POST' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  async uploadProfileFieldLayout(sourceId: number, index: number, json: string): Promise<number> {
    const response = await fetch(`${this.baseUrl}/source/profiles/fieldLayout?sourceId=${sourceId}&index=${index}`, { method: 'POST', body: json });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  // Device settings (settings.json on the coprocessor); a rejected value comes back as the server's explanation
  async getDeviceSettings(): Promise<DeviceSettingsData> {
    const response = await fetch(`${this.baseUrl}/device/settings`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async putDeviceSettings(settings: DeviceSettingsData): Promise<DeviceSettingsData> {
    const response = await fetch(`${this.baseUrl}/device/settings`, {
      method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(settings),
    });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  // A NetworkTablesSink that connects the way the device's settings say
  async createNetworkTablesSinkFromSettings(name: string): Promise<number> {
    const response = await fetch(`${this.baseUrl}/networkTablesSink/createFromSettings?name=${encodeURIComponent(name)}`, { method: 'POST' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  // Network: hostname and IPv4 (NetworkManager). Failures reject with the server's explanation.
  async getNetwork(): Promise<NetworkStatus> {
    const response = await fetch(`${this.baseUrl}/network`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async setHostname(name: string): Promise<void> {
    const response = await fetch(`${this.baseUrl}/network/hostname`, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ Name: name }) });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
  }

  async setIpv4(connection: string, config: Ipv4Config): Promise<PendingNetworkChange> {
    const response = await fetch(`${this.baseUrl}/network/ipv4`, {
      method: 'PUT', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ Connection: connection, Method: config.Method, Address: config.Address, Gateway: config.Gateway, Dns: config.Dns }),
    });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  async confirmNetworkChange(): Promise<void> {
    const response = await fetch(`${this.baseUrl}/network/confirm`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  async revertNetworkChange(): Promise<void> {
    const response = await fetch(`${this.baseUrl}/network/revert`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Snapshot Controller routes (/api/snapshot*): stills saved under snapshots/<camera>/ (SnapshotController.cs)
  async listSnapshots(): Promise<SnapshotEntry[]> {
    const response = await fetch(`${this.baseUrl}/snapshot/list`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  // Saves a still of the camera now; "output" is the detector's annotated frame. Resolves to the new path, or null when no frame exists yet.
  async takeSnapshot(sourceId: number, kind: 'input' | 'output'): Promise<string | null> {
    const response = await fetch(`${this.baseUrl}/snapshot/take?SourceID=${sourceId}&Kind=${kind}`, { method: 'POST' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  getSnapshotUrl(path: string): string {
    return `${this.baseUrl}/snapshot/file?Path=${encodeURIComponent(path)}`;
  }

  async deleteSnapshot(path: string): Promise<void> {
    const response = await fetch(`${this.baseUrl}/snapshot?Path=${encodeURIComponent(path)}`, { method: 'DELETE' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

  // Copies a detector node as "<name> copy"; with copyBindings it is bound to the same source. Resolves to the new sink id; rejects
  // with the server's explanation (for example that this node type cannot be copied).
  async duplicateSink(sinkId: number, copyBindings: boolean): Promise<number> {
    const response = await fetch(`${this.baseUrl}/sink/duplicate?SinkID=${sinkId}&copyBindings=${copyBindings}`, { method: 'POST' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  // Copies a file source (a camera cannot be opened twice); resolves to the new source id.
  async duplicateSource(sourceId: number): Promise<number> {
    const response = await fetch(`${this.baseUrl}/source/duplicate?SourceID=${sourceId}`, { method: 'POST' });
    if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
    return response.json();
  }

  async getDriverMode(sinkId: number): Promise<boolean> {
    const response = await fetch(`${this.baseUrl}/sink/driverMode?SinkID=${sinkId}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async setDriverMode(sinkId: number, enabled: boolean): Promise<void> {
    const response = await fetch(`${this.baseUrl}/sink/driverMode?SinkID=${sinkId}&Enabled=${enabled}`, { method: 'PATCH' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  }

}