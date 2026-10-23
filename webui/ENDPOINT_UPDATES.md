# Web Interface Endpoint Updates

This document describes the updates made to the React web interface to work with the new HTTP endpoints from the C# server.

## Updated Files

### 1. `src/services/ApiService.ts`
- **Updated all API endpoints** to match the new C# controller structure
- **Added correct endpoint paths** based on the actual Route attributes in the controllers
- **Added device monitoring endpoints** (`/api/device/cpuUsage`, `/api/device/ramUsage`, `/api/device/diskUsage`)
- **Added UDP control endpoints** (`/api/udp/start`, `/api/udp/stop`)
- **Improved error handling** for missing endpoints
- **Added utility methods** to aggregate data from multiple source endpoints

### 2. `src/hooks/useAppData.ts`
- **Updated data loading logic** to work with the new API structure
- **Added device stats loading** and integration with system stats
- **Improved source loading** by combining data from different controller endpoints
- **Added local state management** for sinks since there's no general "get all sinks" endpoint
- **Enhanced error handling** and fallback mechanisms
- **Added UDP transmission controls**

### 3. `src/components/AddSourceModal.tsx`
- **Updated camera loading** to use the correct endpoint (`/api/getNotRegistered`)
- **Improved error handling** for camera enumeration
- **Added better user feedback** for when no cameras are available
- **Updated API service usage** to match the new structure

### 4. `src/types/index.ts`
- **Added DeviceStats interface** for CPU, RAM, and disk usage monitoring
- **Enhanced SystemStats interface** to include device monitoring data
- **Updated Source interface** to support additional properties from different source types

### 5. `src/App.tsx`
- **Added device monitoring displays** in the dashboard
- **Added UDP transmission controls** in the device stats section
- **Enhanced system status display** with device metrics
- **Updated dashboard to show device statistics** (RAM, disk usage, CPU)

## Key Changes in API Structure

### Source Management
- **General source operations**: `/api/getAll`, `/api/delete`, `/api/rename`
- **Camera sources**: `/api/getRegistered`, `/api/getNotRegistered`, `/api/create`, `/api/createAll`
- **Video file sources**: `/api/getAll`, `/api/create`, `/api/changeFPS`
- **Image file sources**: `/api/getAll`, `/api/create`

### Sink Management
- **Sink operations**: `/api/bind`, `/api/unbind`, `/api/delete`
- **AprilTag sinks**: `/api/create` (currently the only implemented sink type)

### Device Monitoring
- **CPU Usage**: `/api/device/cpuUsage`
- **RAM Usage**: `/api/device/ramUsage`
- **Disk Usage**: `/api/device/diskUsage`

### UDP Transmission
- **Start UDP**: `/api/udp/start`
- **Stop UDP**: `/api/udp/stop`

## Limitations Addressed

1. **No general "get all sinks" endpoint**: The web interface now manages sinks locally after creation
2. **Limited sink types**: Only AprilTag sinks are currently implemented in the API
3. **No WebRTC streaming**: Streaming functionality is disabled with appropriate user messages
4. **No sink renaming**: This functionality is not available in the current API

## Features Added

1. **Real-time device monitoring**: CPU, RAM, and disk usage display
2. **UDP transmission controls**: Start/stop UDP transmission from the dashboard
3. **Enhanced source management**: Support for camera, video file, and image file sources
4. **Better error handling**: Graceful fallbacks when endpoints are not available
5. **Local state management**: For data that cannot be fetched from the server

## Testing Notes

- The application gracefully handles missing endpoints
- Device monitoring integrates with the existing dashboard
- Source creation works with the new multi-endpoint structure
- UDP controls provide immediate feedback
- Error messages are informative about missing functionality

## Future Considerations

When additional sink types and WebRTC functionality are implemented in the C# server:

1. Update the sink type options in `AddSinkModal`
2. Enable WebRTC streaming controls in the dashboard
3. Add an endpoint for retrieving all sinks
4. Implement sink renaming functionality
5. Add support for additional device monitoring metrics