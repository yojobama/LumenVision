// GENERATED FILE - do not edit by hand.
// Regenerate with: npm run generate-api (webui/scripts/generate-api.mjs)
// Source: Server/OpenApi/OpenApiGenerator.cs's reflection-generated /api/openapi.json.

export interface paths {
    "/apriltagSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ApriltagSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/createFromCamera": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ApriltagSinkController_CreateFromCamera"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/createWithBackend": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ApriltagSinkController_CreateWithBackend"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/backend": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ApriltagSinkController_GetBackend"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["ApriltagSinkController_SetBackend"];
        trace?: never;
    };
    "/apriltagSink/backendKind": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ApriltagSinkController_GetBackendKind"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/tuning": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ApriltagSinkController_GetTuning"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/fieldLayout": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ApriltagSinkController_SetFieldLayout"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/fieldLayouts": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ApriltagSinkController_ListFieldLayouts"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/fieldLayoutBundled": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ApriltagSinkController_SetBundledFieldLayout"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/apriltagSink/fieldLayoutTagCount": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ApriltagSinkController_GetFieldLayoutTagCount"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/camera/start": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_StartCamera"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/stereo/start": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_StartStereo"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/stereo/startSplit": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_StartStereoSplit"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/sessions": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetSessions"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetSession"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/stop": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_Stop"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/saveDetection": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_SaveDetection"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/count": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetCount"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/entry": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete: operations["CalibrationController_RemoveEntry"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/entries": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete: operations["CalibrationController_ClearEntries"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/runCamera": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_RunCamera"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/runStereo": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CalibrationController_RunStereo"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/cameraResult": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetCameraResult"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/stereoResult": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetStereoResult"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/coverage": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetCoverage"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/{id}/status": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetStatus"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/saved": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetSaved"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/calibration/savedStereo": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CalibrationController_GetSavedStereo"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/createAll": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CameraSourceController_CreateAll"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CameraSourceController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/getRegistered": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetRegistered"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/getNotRegistered": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetNotRegistered"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/{id}/modes": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetModes"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/{id}/currentMode": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetCurrentMode"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/{id}/calibrationStatus": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetCalibrationStatus"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/{id}/mode": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["CameraSourceController_SetMode"];
        trace?: never;
    };
    "/cameraSource/{id}/exposure": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["CameraSourceController_SetExposure"];
        trace?: never;
    };
    "/cameraSource/{id}/autoExposure": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["CameraSourceController_SetAutoExposure"];
        trace?: never;
    };
    "/cameraSource/{id}/gain": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["CameraSourceController_SetGain"];
        trace?: never;
    };
    "/cameraSource/{id}/controls": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetControls"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/{id}/controlList": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetControlList"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/cameraSource/{id}/control": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["CameraSourceController_SetControl"];
        trace?: never;
    };
    "/cameraSource/{id}/transform": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CameraSourceController_GetTransform"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["CameraSourceController_SetTransform"];
        trace?: never;
    };
    "/cameraSource/{id}/roi": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["CameraSourceController_CreateRoi"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/capabilities/features": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CapabilitiesController_GetEnabledFeatures"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/capabilities/nodeTypes": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["CapabilitiesController_GetNodeTypes"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/depthFusionSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["DepthFusionSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/depthFusionSink/{id}/attachDepthSource": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["DepthFusionSinkController_AttachDepthSource"];
        trace?: never;
    };
    "/device/cpuUsage": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceController_GetDeviceCPUUsage"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/ramUsage": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceController_GetDeviceRamUsage"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/diskUsage": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceController_GetDeviceDiskUsage"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/accelerators": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceController_GetAccelerators"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/temperature": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceController_GetDeviceTemperature"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/version": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceSettingsController_GetVersion"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/settings": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["DeviceSettingsController_Get"];
        put: operations["DeviceSettingsController_Put"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/graphProfile/list": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["GraphProfileController_List"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/graphProfile/saveCurrentAs": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["GraphProfileController_SaveCurrentAs"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/graphProfile/activate": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["GraphProfileController_Activate"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/imageFileSource/get": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ImageFileSourceController_GetAll"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/imageFileSource/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ImageFileSourceController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/log/tail": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["LogController_Tail"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/log/entries": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["LogController_Entries"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/log/download": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["LogController_Download"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/mjpegSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["MjpegSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/mjpegSink/settings": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["MjpegSinkController_SetSettings"];
        trace?: never;
    };
    "/model/upload": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ModelController_Upload"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/model/getAll": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ModelController_GetAll"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/model/update": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["ModelController_Update"];
        trace?: never;
    };
    "/model/delete": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete: operations["ModelController_Delete"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["NetworkController_Get"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network/hostname": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put: operations["NetworkController_SetHostname"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network/ipv4": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put: operations["NetworkController_SetIpv4"];
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network/confirm": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["NetworkController_Confirm"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/network/revert": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["NetworkController_Revert"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/networkTablesSink/createForTeam": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["NetworkTablesSinkController_CreateForTeam"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/networkTablesSink/createForServer": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["NetworkTablesSinkController_CreateForServer"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/networkTablesSink/createFromSettings": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["NetworkTablesSinkController_CreateFromSettings"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/networkTablesSink/status": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["NetworkTablesSinkController_GetStatus"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/objectDetectionSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["ObjectDetectionSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/objectDetectionSink/thresholds": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ObjectDetectionSinkController_GetThresholds"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["ObjectDetectionSinkController_SetThresholds"];
        trace?: never;
    };
    "/objectDetectionSink/backend": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["ObjectDetectionSinkController_GetBackend"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/openapi.json": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["OpenApiController_GetOpenApiDocument"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/apriltag": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PipelineProfileController_CreateApriltagProfile"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/objectDetection": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PipelineProfileController_CreateObjectDetectionProfile"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/cameraOverrides": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["PipelineProfileController_SetCameraOverrides"];
        trace?: never;
    };
    "/source/profiles/fieldLayout": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PipelineProfileController_SetProfileFieldLayout"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/fieldLayoutBundled": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PipelineProfileController_SetProfileBundledFieldLayout"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/fieldLayoutTagCount": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["PipelineProfileController_GetProfileFieldLayoutTagCount"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["PipelineProfileController_GetProfiles"];
        put?: never;
        post?: never;
        delete: operations["PipelineProfileController_DeleteProfile"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/active": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["PipelineProfileController_GetActiveProfile"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/profiles/activate": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["PipelineProfileController_Activate"];
        trace?: never;
    };
    "/device/restart": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PowerController_Restart"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/reboot": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PowerController_Reboot"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/shutdown": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PowerController_Shutdown"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/factoryReset": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["PowerController_FactoryReset"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/recordSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["RecordSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/recordSink/{id}/segments": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["RecordSinkController_GetSegments"];
        put?: never;
        post?: never;
        delete: operations["RecordSinkController_DeleteSegment"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/recordSink/{id}/download": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["RecordSinkController_Download"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/recordSink/all": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["RecordSinkController_IsAnyRecording"];
        put?: never;
        post: operations["RecordSinkController_SetAllRecording"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/recordSink/{id}/promote": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["RecordSinkController_Promote"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/settings/export": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SettingsArchiveController_Export"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/settings/import": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["SettingsArchiveController_Import"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/getStatus": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SinkController_GetStatus"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/getResult": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SinkController_GetResult"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/getAllResults": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SinkController_GetAllResults"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/toggle": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SinkController_Toggle"];
        trace?: never;
    };
    "/sink/rename": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SinkController_Rename"];
        trace?: never;
    };
    "/sink/bind": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SinkController_Bind"];
        trace?: never;
    };
    "/sink/unbind": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SinkController_Unbind"];
        trace?: never;
    };
    "/sink/delete": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete: operations["SinkController_Delete"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/getAll": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SinkController_GetAll"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/duplicate": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["SinkController_Duplicate"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/sink/driverMode": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SinkController_GetDriverMode"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SinkController_SetDriverMode"];
        trace?: never;
    };
    "/snapshot/list": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SnapshotController_List"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/snapshot/file": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SnapshotController_Download"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/snapshot": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete: operations["SnapshotController_Delete"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/snapshot/take": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["SnapshotController_Take"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/isActive": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SourceController_IsActive"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/getAll": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SourceController_GetAllSources"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/rename": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SourceController_Rename"];
        trace?: never;
    };
    "/source/delete": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete: operations["SourceController_Delete"];
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/fpsLimit": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["SourceController_GetFpsLimit"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["SourceController_SetFpsLimit"];
        trace?: never;
    };
    "/source/duplicate": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["SourceController_Duplicate"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/source/snapshot": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["SourceController_SaveSnapshot"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/stereoDepthSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["StereoDepthSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/stereoDepthSink/{id}/bind": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["StereoDepthSinkController_Bind"];
        trace?: never;
    };
    "/stereoDepthSink/{id}/backendName": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["StereoDepthSinkController_GetBackendName"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/stereoDepthSink/{id}/stats": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["StereoDepthSinkController_GetStats"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/update/upload": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["UpdateController_Upload"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/update/install": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["UpdateController_Install"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/device/update/status": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["UpdateController_Status"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/videoFileSource/getAll": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["VideoFileSourceController_GetAll"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/videoFileSource/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["VideoFileSourceController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/videoFileSource/changeFPS": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["VideoFileSourceController_ChangeFPS"];
        trace?: never;
    };
    "/webrtcSink/create": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["WebRTCSinkController_Create"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/webrtcSink/settings": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch: operations["WebRTCSinkController_SetSettings"];
        trace?: never;
    };
    "/webrtcSink/offer": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["WebRTCSinkController_CreateOffer"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/webrtcSink/answer": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["WebRTCSinkController_SetAnswer"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/webrtcSink/candidate": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get?: never;
        put?: never;
        post: operations["WebRTCSinkController_AddIceCandidate"];
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/webrtcSink/status": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["WebRTCSinkController_GetStatus"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
    "/webrtcSink/preferredEncoder": {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        get: operations["WebRTCSinkController_GetPreferredEncoder"];
        put?: never;
        post?: never;
        delete?: never;
        options?: never;
        head?: never;
        patch?: never;
        trace?: never;
    };
}
export type webhooks = Record<string, never>;
export interface components {
    schemas: {
        ApriltagTuningDto: {
            Threads: number;
            QuadDecimate: number;
            QuadDecimateSupported: boolean;
            RefineEdges: boolean;
            RefineMode: number;
            RefineModeSupported: boolean;
            Family: number;
            QuadSigma: number;
            QuadSigmaSupported: boolean;
            MaxHamming: number;
            DecisionMargin: number;
            PoseIterations: number;
            MultiTag: boolean;
            SingleTagPose: boolean;
        };
        FieldLayoutInfo: {
            Id?: string;
            Name?: string;
            TagCount: number;
        };
        CalibrationSessionDto: {
            SessionId: number;
            Kind?: string;
            PreviewSinkId: number;
        };
        CameraCalibrationResultDto: {
            Fx: number;
            Fy: number;
            Cx: number;
            Cy: number;
            Rms: number;
            DistCoeffs?: number[];
            ImageWidth: number;
            ImageHeight: number;
        };
        StereoCalibrationResultDto: {
            Left: components["schemas"]["CameraCalibrationResultDto"];
            Right: components["schemas"]["CameraCalibrationResultDto"];
            R?: number[];
            T?: number[];
            E?: number[];
            F?: number[];
            R1?: number[];
            R2?: number[];
            P1?: number[];
            P2?: number[];
            Q?: number[];
            StereoRms: number;
            EpipolarRms: number;
            BaselineMeters: number;
            RectifiedFx: number;
            RectifiedCx: number;
            RectifiedCy: number;
            ImageWidth: number;
            ImageHeight: number;
            RoiLeftX: number;
            RoiLeftY: number;
            RoiLeftW: number;
            RoiLeftH: number;
            RoiRightX: number;
            RoiRightY: number;
            RoiRightW: number;
            RoiRightH: number;
        };
        CalibrationCoverageDto: {
            FrameWidth: number;
            FrameHeight: number;
            Snapshots?: number[][];
        };
        StoredCalibrationDto: {
            CameraPath?: string;
            Result: components["schemas"]["CameraCalibrationResultDto"];
            CalibratedAtUnixMs: number;
        };
        StoredStereoCalibrationDto: {
            LeftCameraPath?: string;
            RightCameraPath?: string;
            Result: components["schemas"]["StereoCalibrationResultDto"];
            CalibratedAtUnixMs: number;
        };
        CameraHardwareInfo: {
            name?: string;
            path?: string;
        };
        ApriltagAdvancedTuning: {
            Family?: number;
            QuadSigma?: number;
            MaxHamming?: number;
            DecisionMargin?: number;
            PoseIterations?: number;
            MultiTag?: boolean;
            SingleTagPose?: boolean;
            IsDefault: boolean;
        };
        FrameTransformDto: {
            Rotation: number;
            FlipHorizontal: boolean;
            FlipVertical: boolean;
            CropX: number;
            CropY: number;
            CropWidth: number;
            CropHeight: number;
            IsIdentity: boolean;
        };
        CameraOverrides: {
            ControlValues?: number[];
            Transform?: components["schemas"]["FrameTransformDto"];
            FpsLimit?: number;
        };
        PipelineProfile: {
            Index: number;
            Name?: string;
            Kind: number;
            TagSize?: number;
            Backend?: number;
            FrameWidth: number;
            FrameHeight: number;
            Threads?: number;
            QuadDecimate?: number;
            RefineEdges?: boolean;
            RefineMode?: number;
            Advanced?: components["schemas"]["ApriltagAdvancedTuning"];
            FieldLayoutPath?: string;
            DriverMode: boolean;
            ModelId?: number;
            ConfThreshold?: number;
            NmsThreshold?: number;
            CameraOverrides?: components["schemas"]["CameraOverrides"];
        };
        Source: {
            CameraHardwareInfo?: components["schemas"]["CameraHardwareInfo"];
            Fps?: number;
            FilePath?: string;
            Profiles?: components["schemas"]["PipelineProfile"][];
            ActiveProfileIndex: number;
            ActiveDetectionSinkId?: number;
            FpsLimit?: number;
            ControlValues?: number[];
            Transform?: components["schemas"]["FrameTransformDto"];
            Type: number;
            Id: number;
            Name?: string;
        };
        CameraHardwareInfoDto: {
            Name?: string;
            Path?: string;
        };
        CameraModeDto: {
            Width: number;
            Height: number;
            Fps: number;
            PixelFormat: number;
            IsNative: boolean;
        };
        CalibrationStatusDto: {
            HasCalibration: boolean;
            MatchesCurrentResolution: boolean;
            CalibratedWidth?: number;
            CalibratedHeight?: number;
        };
        CameraControlRangeDto: {
            Supported: boolean;
            Minimum: number;
            Maximum: number;
            Step: number;
            Default: number;
            Value: number;
        };
        CameraControlsDto: {
            Exposure: components["schemas"]["CameraControlRangeDto"];
            Gain: components["schemas"]["CameraControlRangeDto"];
        };
        CameraControlDto: {
            Id: number;
            Name?: string;
            Kind: number;
            Minimum: number;
            Maximum: number;
            Step: number;
            Default: number;
            Value: number;
            ReadOnly: boolean;
            Inactive: boolean;
            MenuLabels?: string[];
            MenuValues?: number[];
        };
        NodeTypeCapability: {
            TypeName?: string;
            Category?: string;
            DisplayName?: string;
            Icon?: string;
            MaxSources: number;
            SourceRoles?: string[];
            IsDualRoleSink: boolean;
            HasDepthAttach: boolean;
            Implemented: boolean;
        };
        NodeTypesResponse: {
            Sources?: components["schemas"]["NodeTypeCapability"][];
            Sinks?: components["schemas"]["NodeTypeCapability"][];
        };
        AcceleratorInfo: {
            Kind?: string;
            Name?: string;
            FreqMhz: number;
            MaxFreqMhz: number;
            Governor?: string;
            LoadPercent?: number;
        };
        VersionInfo: {
            Server?: string;
            LumenCore?: string;
            Os?: string;
            Kernel?: string;
            Architecture?: string;
            Runtime?: string;
            Hostname?: string;
        };
        LedSettings: {
            Enabled: boolean;
            Chip: number;
            Line: number;
            ActiveLow: boolean;
        };
        NetworkTablesSettings: {
            Mode?: string;
            TeamNumber?: number;
            ServerAddress?: string;
            Port: number;
            RootTable?: string;
            ClientIdentity?: string;
        };
        DeviceSettingsData: {
            Led?: components["schemas"]["LedSettings"];
            NetworkTables?: components["schemas"]["NetworkTablesSettings"];
        };
        TimeSpan: {
            Ticks: number;
            Days: number;
            Hours: number;
            Milliseconds: number;
            Microseconds: number;
            Nanoseconds: number;
            Minutes: number;
            Seconds: number;
            TotalDays: number;
            TotalHours: number;
            TotalMilliseconds: number;
            TotalMicroseconds: number;
            TotalNanoseconds: number;
            TotalMinutes: number;
            TotalSeconds: number;
        };
        DateTime: {
            Date: components["schemas"]["DateTime"];
            Day: number;
            DayOfWeek: number;
            DayOfYear: number;
            Hour: number;
            Kind: number;
            Millisecond: number;
            Microsecond: number;
            Nanosecond: number;
            Minute: number;
            Month: number;
            Second: number;
            Ticks: number;
            TimeOfDay: components["schemas"]["TimeSpan"];
            Year: number;
        };
        LogEntry: {
            Id: number;
            TimeUtc: components["schemas"]["DateTime"];
            Level: number;
            Source?: string;
            Message?: string;
        };
        Model: {
            Id: number;
            Name?: string;
            ModelPath?: string;
            LabelsPath?: string;
            Variant: number;
            InputSize: number;
            ConfThreshold: number;
            NmsThreshold: number;
            Provider: number;
        };
        Ipv4Config: {
            Method?: string;
            Address?: string;
            Gateway?: string;
            Dns?: string[];
        };
        NetworkConnectionInfo: {
            Name?: string;
            Device?: string;
            Type?: string;
            Configured?: components["schemas"]["Ipv4Config"];
            CurrentAddresses?: string[];
        };
        PendingNetworkChange: {
            Connection?: string;
            SecondsLeft: number;
        };
        NetworkStatus: {
            Supported: boolean;
            Hostname?: string;
            Connections?: components["schemas"]["NetworkConnectionInfo"][];
            Pending?: components["schemas"]["PendingNetworkChange"];
        };
        NetworkTablesStatusDto: {
            Connected: boolean;
            Identity?: string;
            RootTable?: string;
            TeamNumber?: number;
            ServerAddress?: string;
        };
        ObjectDetectionThresholdsDto: {
            ConfThreshold: number;
            NmsThreshold: number;
            ModelId?: number;
        };
        RecordSegmentDto: {
            FileName?: string;
            SizeBytes: number;
            LastWriteTimeUtc: components["schemas"]["DateTime"];
        };
        ImportResult: {
            FilesRestored: number;
            Groups?: string[];
            BackupPath?: string;
        };
        Sink: {
            Type: number;
            Id: number;
            Name?: string;
            Source?: components["schemas"]["Source"];
            Source2?: components["schemas"]["Source"];
            DepthSourceId?: number;
            RecordDstFolder?: string;
            RecordEncoderName?: string;
            RecordBitrateKbps?: number;
            RecordSegmentSeconds?: number;
            RecordMaxFolderSizeBytes?: number;
            RecordMaxFileCount?: number;
            ApriltagTagSize?: number;
            ApriltagBackend?: number;
            ApriltagThreads?: number;
            ApriltagQuadDecimate?: number;
            ApriltagRefineEdges?: boolean;
            ApriltagRefineMode?: number;
            ApriltagAdvanced?: components["schemas"]["ApriltagAdvancedTuning"];
            StreamJpegQuality?: number;
            StreamBitrateKbps?: number;
            StreamFps?: number;
            StreamScaleDivisor?: number;
            ObjectDetectionModelId?: number;
            ObjectDetectionConfThreshold?: number;
            ObjectDetectionNmsThreshold?: number;
        };
        SnapshotEntry: {
            Camera?: string;
            Path?: string;
            Kind?: string;
            SizeBytes: number;
            CreatedUtc: components["schemas"]["DateTime"];
        };
        StereoDepthStatsDto: {
            ValidFraction: number;
            MedianDepthMeters: number;
        };
        StagedPackage: {
            Package?: string;
            Version?: string;
            Architecture?: string;
            SizeBytes: number;
        };
        UpdateStatus: {
            State?: string;
            Log?: string;
        };
        WebRtcStatusDto: {
            Connected: boolean;
            IceState: number;
            GatheringComplete: boolean;
        };
    };
    responses: never;
    parameters: never;
    requestBodies: never;
    headers: never;
    pathItems: never;
}
export type $defs = Record<string, never>;
export interface operations {
    ApriltagSinkController_Create: {
        parameters: {
            query?: {
                name?: string;
                type?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ApriltagSinkController_CreateFromCamera: {
        parameters: {
            query: {
                name?: string;
                sourceId: number;
                tagSize: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ApriltagSinkController_CreateWithBackend: {
        parameters: {
            query: {
                name?: string;
                tagSize: number;
                backend: number;
                frameWidth?: number;
                frameHeight?: number;
                nthreads?: number;
                quadDecimate?: number;
                refineEdges?: boolean;
                refineMode?: number;
                family?: number;
                quadSigma?: number;
                maxHamming?: number;
                decisionMargin?: number;
                poseIterations?: number;
                multiTag?: boolean;
                singleTagPose?: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ApriltagSinkController_GetBackend: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string;
                };
            };
        };
    };
    ApriltagSinkController_SetBackend: {
        parameters: {
            query: {
                sinkId: number;
                backend: number;
                nthreads?: number;
                quadDecimate?: number;
                refineEdges?: boolean;
                refineMode?: number;
                family?: number;
                quadSigma?: number;
                maxHamming?: number;
                decisionMargin?: number;
                poseIterations?: number;
                multiTag?: boolean;
                singleTagPose?: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    ApriltagSinkController_GetBackendKind: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ApriltagSinkController_GetTuning: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["ApriltagTuningDto"];
                };
            };
        };
    };
    ApriltagSinkController_SetFieldLayout: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ApriltagSinkController_ListFieldLayouts: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["FieldLayoutInfo"][];
                };
            };
        };
    };
    ApriltagSinkController_SetBundledFieldLayout: {
        parameters: {
            query: {
                sinkId: number;
                layout?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ApriltagSinkController_GetFieldLayoutTagCount: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    CalibrationController_StartCamera: {
        parameters: {
            query: {
                sourceId: number;
                boardType?: number;
                rows?: number;
                cols?: number;
                squareSizeMeters?: number;
                markerSizeMeters?: number;
                arucoDictionaryId?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationSessionDto"];
                };
            };
        };
    };
    CalibrationController_StartStereo: {
        parameters: {
            query: {
                leftSourceId: number;
                rightSourceId: number;
                boardType?: number;
                rows?: number;
                cols?: number;
                squareSizeMeters?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationSessionDto"];
                };
            };
        };
    };
    CalibrationController_StartStereoSplit: {
        parameters: {
            query: {
                sourceId: number;
                boardType?: number;
                rows?: number;
                cols?: number;
                squareSizeMeters?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationSessionDto"];
                };
            };
        };
    };
    CalibrationController_GetSessions: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationSessionDto"][];
                };
            };
        };
    };
    CalibrationController_GetSession: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationSessionDto"];
                };
            };
        };
    };
    CalibrationController_Stop: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    CalibrationController_SaveDetection: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CalibrationController_GetCount: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    CalibrationController_RemoveEntry: {
        parameters: {
            query: {
                index: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CalibrationController_ClearEntries: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    CalibrationController_RunCamera: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraCalibrationResultDto"];
                };
            };
        };
    };
    CalibrationController_RunStereo: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["StereoCalibrationResultDto"];
                };
            };
        };
    };
    CalibrationController_GetCameraResult: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraCalibrationResultDto"];
                };
            };
        };
    };
    CalibrationController_GetStereoResult: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["StereoCalibrationResultDto"];
                };
            };
        };
    };
    CalibrationController_GetCoverage: {
        parameters: {
            query?: {
                eye?: string;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationCoverageDto"];
                };
            };
        };
    };
    CalibrationController_GetStatus: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string;
                };
            };
        };
    };
    CalibrationController_GetSaved: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["StoredCalibrationDto"][];
                };
            };
        };
    };
    CalibrationController_GetSavedStereo: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["StoredStereoCalibrationDto"][];
                };
            };
        };
    };
    CameraSourceController_CreateAll: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    CameraSourceController_Create: {
        parameters: {
            query?: {
                name?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    CameraSourceController_GetRegistered: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Source"][];
                };
            };
        };
    };
    CameraSourceController_GetNotRegistered: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraHardwareInfoDto"][];
                };
            };
        };
    };
    CameraSourceController_GetModes: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraModeDto"][];
                };
            };
        };
    };
    CameraSourceController_GetCurrentMode: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraModeDto"];
                };
            };
        };
    };
    CameraSourceController_GetCalibrationStatus: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CalibrationStatusDto"];
                };
            };
        };
    };
    CameraSourceController_SetMode: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CameraSourceController_SetExposure: {
        parameters: {
            query: {
                exposureAbsolute: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CameraSourceController_SetAutoExposure: {
        parameters: {
            query: {
                enabled: boolean;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CameraSourceController_SetGain: {
        parameters: {
            query: {
                gain: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CameraSourceController_GetControls: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraControlsDto"];
                };
            };
        };
    };
    CameraSourceController_GetControlList: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["CameraControlDto"][];
                };
            };
        };
    };
    CameraSourceController_SetControl: {
        parameters: {
            query: {
                controlId: number;
                value: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    CameraSourceController_GetTransform: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["FrameTransformDto"];
                };
            };
        };
    };
    CameraSourceController_SetTransform: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    CameraSourceController_CreateRoi: {
        parameters: {
            query: {
                x: number;
                y: number;
                width: number;
                height: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    CapabilitiesController_GetEnabledFeatures: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string[];
                };
            };
        };
    };
    CapabilitiesController_GetNodeTypes: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["NodeTypesResponse"];
                };
            };
        };
    };
    DepthFusionSinkController_Create: {
        parameters: {
            query?: {
                name?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    DepthFusionSinkController_AttachDepthSource: {
        parameters: {
            query: {
                stereoDepthSinkId: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    DeviceController_GetDeviceCPUUsage: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    DeviceController_GetDeviceRamUsage: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    DeviceController_GetDeviceDiskUsage: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    DeviceController_GetAccelerators: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["AcceleratorInfo"][];
                };
            };
        };
    };
    DeviceController_GetDeviceTemperature: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    DeviceSettingsController_GetVersion: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["VersionInfo"];
                };
            };
        };
    };
    DeviceSettingsController_Get: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["DeviceSettingsData"];
                };
            };
        };
    };
    DeviceSettingsController_Put: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["DeviceSettingsData"];
                };
            };
        };
    };
    GraphProfileController_List: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string[];
                };
            };
        };
    };
    GraphProfileController_SaveCurrentAs: {
        parameters: {
            query?: {
                name?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    GraphProfileController_Activate: {
        parameters: {
            query?: {
                name?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    ImageFileSourceController_GetAll: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Source"][];
                };
            };
        };
    };
    ImageFileSourceController_Create: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number[];
                };
            };
        };
    };
    LogController_Tail: {
        parameters: {
            query?: {
                lines?: number;
                level?: string;
                since?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string[];
                };
            };
        };
    };
    LogController_Entries: {
        parameters: {
            query?: {
                lines?: number;
                level?: string;
                since?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["LogEntry"][];
                };
            };
        };
    };
    LogController_Download: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    MjpegSinkController_Create: {
        parameters: {
            query?: {
                name?: string;
                jpegQuality?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    MjpegSinkController_SetSettings: {
        parameters: {
            query: {
                sinkId: number;
                jpegQuality?: number;
                scaleDivisor?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    ModelController_Upload: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ModelController_GetAll: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Model"][];
                };
            };
        };
    };
    ModelController_Update: {
        parameters: {
            query: {
                id: number;
                name?: string;
                confThreshold?: number;
                nmsThreshold?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Model"];
                };
            };
        };
    };
    ModelController_Delete: {
        parameters: {
            query: {
                id: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    NetworkController_Get: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["NetworkStatus"];
                };
            };
        };
    };
    NetworkController_SetHostname: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    NetworkController_SetIpv4: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["PendingNetworkChange"];
                };
            };
        };
    };
    NetworkController_Confirm: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    NetworkController_Revert: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    NetworkTablesSinkController_CreateForTeam: {
        parameters: {
            query: {
                name?: string;
                teamNumber: number;
                rootTable?: string;
                clientIdentity?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    NetworkTablesSinkController_CreateForServer: {
        parameters: {
            query?: {
                name?: string;
                serverAddress?: string;
                port?: number;
                rootTable?: string;
                clientIdentity?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    NetworkTablesSinkController_CreateFromSettings: {
        parameters: {
            query?: {
                name?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    NetworkTablesSinkController_GetStatus: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["NetworkTablesStatusDto"];
                };
            };
        };
    };
    ObjectDetectionSinkController_Create: {
        parameters: {
            query: {
                name?: string;
                modelId: number;
                confThreshold?: number;
                nmsThreshold?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    ObjectDetectionSinkController_GetThresholds: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["ObjectDetectionThresholdsDto"];
                };
            };
        };
    };
    ObjectDetectionSinkController_SetThresholds: {
        parameters: {
            query: {
                sinkId: number;
                confThreshold: number;
                nmsThreshold: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    ObjectDetectionSinkController_GetBackend: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string;
                };
            };
        };
    };
    OpenApiController_GetOpenApiDocument: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PipelineProfileController_CreateApriltagProfile: {
        parameters: {
            query: {
                sourceId: number;
                name?: string;
                tagSize: number;
                backend?: number;
                frameWidth?: number;
                frameHeight?: number;
                driverMode?: boolean;
                nthreads?: number;
                quadDecimate?: number;
                refineEdges?: boolean;
                refineMode?: number;
                family?: number;
                quadSigma?: number;
                maxHamming?: number;
                decisionMargin?: number;
                poseIterations?: number;
                multiTag?: boolean;
                singleTagPose?: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    PipelineProfileController_CreateObjectDetectionProfile: {
        parameters: {
            query: {
                sourceId: number;
                name?: string;
                modelId: number;
                confThreshold?: number;
                nmsThreshold?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    PipelineProfileController_SetCameraOverrides: {
        parameters: {
            query: {
                sourceId: number;
                index: number;
                enabled: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PipelineProfileController_SetProfileFieldLayout: {
        parameters: {
            query: {
                sourceId: number;
                index: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    PipelineProfileController_SetProfileBundledFieldLayout: {
        parameters: {
            query: {
                sourceId: number;
                index: number;
                layout?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    PipelineProfileController_GetProfileFieldLayoutTagCount: {
        parameters: {
            query: {
                sourceId: number;
                index: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    PipelineProfileController_GetProfiles: {
        parameters: {
            query: {
                sourceId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["PipelineProfile"][];
                };
            };
        };
    };
    PipelineProfileController_DeleteProfile: {
        parameters: {
            query: {
                sourceId: number;
                index: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PipelineProfileController_GetActiveProfile: {
        parameters: {
            query: {
                sourceId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    PipelineProfileController_Activate: {
        parameters: {
            query: {
                sourceId: number;
                index: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PowerController_Restart: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PowerController_Reboot: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PowerController_Shutdown: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    PowerController_FactoryReset: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    RecordSinkController_Create: {
        parameters: {
            query?: {
                name?: string;
                dstFolder?: string;
                encoderName?: string;
                bitrateKbps?: number;
                fps?: number;
                segmentSeconds?: number;
                maxFolderSizeBytes?: number;
                maxFileCount?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    RecordSinkController_GetSegments: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["RecordSegmentDto"][];
                };
            };
        };
    };
    RecordSinkController_DeleteSegment: {
        parameters: {
            query?: {
                file?: string;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    RecordSinkController_Download: {
        parameters: {
            query?: {
                file?: string;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    RecordSinkController_IsAnyRecording: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    RecordSinkController_SetAllRecording: {
        parameters: {
            query: {
                enabled: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    RecordSinkController_Promote: {
        parameters: {
            query?: {
                file?: string;
                name?: string;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    SettingsArchiveController_Export: {
        parameters: {
            query?: {
                models?: boolean;
                log?: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SettingsArchiveController_Import: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["ImportResult"];
                };
            };
        };
    };
    SinkController_GetStatus: {
        parameters: {
            query: {
                SinkID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    SinkController_GetResult: {
        parameters: {
            query: {
                SinkID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_GetAllResults: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_Toggle: {
        parameters: {
            query: {
                SinkID: number;
                Enabled: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_Rename: {
        parameters: {
            query: {
                SinkID: number;
                NewName?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_Bind: {
        parameters: {
            query: {
                SinkID: number;
                SourceID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_Unbind: {
        parameters: {
            query: {
                SinkID: number;
                SourceID?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_Delete: {
        parameters: {
            query: {
                SinkID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SinkController_GetAll: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Sink"][];
                };
            };
        };
    };
    SinkController_Duplicate: {
        parameters: {
            query: {
                SinkID: number;
                copyBindings?: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    SinkController_GetDriverMode: {
        parameters: {
            query: {
                SinkID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    SinkController_SetDriverMode: {
        parameters: {
            query: {
                SinkID: number;
                Enabled: boolean;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SnapshotController_List: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["SnapshotEntry"][];
                };
            };
        };
    };
    SnapshotController_Download: {
        parameters: {
            query?: {
                Path?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SnapshotController_Delete: {
        parameters: {
            query?: {
                Path?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    SnapshotController_Take: {
        parameters: {
            query: {
                SourceID: number;
                Kind?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string;
                };
            };
        };
    };
    SourceController_IsActive: {
        parameters: {
            query: {
                SourceID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    SourceController_GetAllSources: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Source"][];
                };
            };
        };
    };
    SourceController_Rename: {
        parameters: {
            query: {
                SourceID: number;
                newName?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SourceController_Delete: {
        parameters: {
            query: {
                SourceID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SourceController_GetFpsLimit: {
        parameters: {
            query: {
                SourceID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    SourceController_SetFpsLimit: {
        parameters: {
            query: {
                SourceID: number;
                fps: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    SourceController_Duplicate: {
        parameters: {
            query: {
                SourceID: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    SourceController_SaveSnapshot: {
        parameters: {
            query: {
                SourceID: number;
                fileName?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": boolean;
                };
            };
        };
    };
    StereoDepthSinkController_Create: {
        parameters: {
            query: {
                name?: string;
                backend: number;
                minDepthMeters: number;
                maxDepthMeters: number;
                maxSkewUs: number;
                frameOutput: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    StereoDepthSinkController_Bind: {
        parameters: {
            query: {
                leftSourceId: number;
                rightSourceId: number;
            };
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    StereoDepthSinkController_GetBackendName: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string;
                };
            };
        };
    };
    StereoDepthSinkController_GetStats: {
        parameters: {
            query?: never;
            header?: never;
            path: {
                id: number;
            };
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["StereoDepthStatsDto"];
                };
            };
        };
    };
    UpdateController_Upload: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["StagedPackage"];
                };
            };
        };
    };
    UpdateController_Install: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    UpdateController_Status: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["UpdateStatus"];
                };
            };
        };
    };
    VideoFileSourceController_GetAll: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["Source"][];
                };
            };
        };
    };
    VideoFileSourceController_Create: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number[];
                };
            };
        };
    };
    VideoFileSourceController_ChangeFPS: {
        parameters: {
            query: {
                fps: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    WebRTCSinkController_Create: {
        parameters: {
            query?: {
                name?: string;
                bitrateKbps?: number;
                fps?: number;
                encoderName?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": number;
                };
            };
        };
    };
    WebRTCSinkController_SetSettings: {
        parameters: {
            query: {
                sinkId: number;
                bitrateKbps?: number;
                fps?: number;
                scaleDivisor?: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    WebRTCSinkController_CreateOffer: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    WebRTCSinkController_SetAnswer: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    WebRTCSinkController_AddIceCandidate: {
        parameters: {
            query: {
                sinkId: number;
                candidate?: string;
                mid?: string;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content?: never;
            };
        };
    };
    WebRTCSinkController_GetStatus: {
        parameters: {
            query: {
                sinkId: number;
            };
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": components["schemas"]["WebRtcStatusDto"];
                };
            };
        };
    };
    WebRTCSinkController_GetPreferredEncoder: {
        parameters: {
            query?: never;
            header?: never;
            path?: never;
            cookie?: never;
        };
        requestBody?: never;
        responses: {
            /** @description OK */
            200: {
                headers: {
                    [name: string]: unknown;
                };
                content: {
                    "application/json": string;
                };
            };
        };
    };
}
