# FRCV WebRTC Video Streaming Implementation

> Written when this project was called FRCV. Left in its original FRCV-branded prose rather
> than rewritten - see `docs/RENAME.md` for the old->new identifier table and why.

## Overview
This implementation adds WebRTC video streaming capability to the FRCV system, allowing real-time streaming of sink preview images with H.264 encoding.

## Features Added

### 1. WebRTC Streaming Service (`WebRTCStreamingService.cs`)
- **WebRTCVideoStreamer**: Manages individual video streams for sinks
- **H.264 Encoding**: Converts preview images to H.264 video stream
- **Real-time Streaming**: 30 FPS video capture and transmission
- **Multiple Connections**: Supports multiple simultaneous clients

### 2. API Endpoints in SinkController
- `POST /api/sink/webrtc/start` - Start WebRTC stream for a sink
- `POST /api/sink/webrtc/answer` - Handle WebRTC answer from client
- `POST /api/sink/webrtc/ice` - Handle ICE candidates
- `POST /api/sink/webrtc/stop` - Stop WebRTC stream
- `GET /api/sink/webrtc/status` - Check stream status
- `PUT /api/sink/preview/enable` - Enable sink preview
- `PUT /api/sink/preview/disable` - Disable sink preview
- `GET /api/sink/preview/image` - Get preview image info

## How It Works

### 1. Stream Initialisation
```
Client                    Server
  |                         |
  |-- POST /webrtc/start -->|
  |                         |-- Enable sink preview
  |                         |-- Create peer connection
  |                         |-- Generate SDP offer
  |<-- SDP Offer -----------|
  |                         |
  |-- WebRTC Answer ------->|
  |                         |
  |<-- ICE Candidates ----->|
  |                         |
  |<== Video Stream ========|
```

### 2. Video Pipeline
1. **Frame Capture**: Get preview image from sink (30 FPS)
2. **Image Conversion**: Convert Image8U to RGB byte array
3. **Colour Space**: Convert RGB to YUV420 (I420) format
4. **H.264 Encoding**: Encode frame with SPS/PPS/IDR NAL units
5. **RTP Transmission**: Package and send via WebRTC

### 3. API Usage Examples

#### Start Streaming
```bash
curl -X POST "http://localhost:8175/api/sink/webrtc/start?sinkId=1&connectionId=conn_123"
```

#### Stop Streaming
```bash
curl -X POST "http://localhost:8175/api/sink/webrtc/stop?sinkId=1"
```

#### Check Status
```bash
curl "http://localhost:8175/api/sink/webrtc/status?sinkId=1"
```

## Technical Implementation Details

### WebRTC Configuration
- **STUN Servers**: Google STUN servers for NAT traversal
- **Video Codec**: H.264 (payload type 96)
- **Sample Rate**: 90kHz for video
- **ICE**: Automatic candidate gathering and exchange

### H.264 Encoding (Simplified)
The current implementation includes basic H.264 structure:
- **SPS (Sequence Parameter Set)**: Video parameters
- **PPS (Picture Parameter Set)**: Picture parameters  
- **IDR Frames**: Intra-coded frames for key frames

### Image Processing
- **Colour Conversion**: RGB ? YUV420 using ITU-R BT.601 standard
- **Subsampling**: 4:2:0 chroma subsampling for efficiency
- **Test Pattern**: Gradient pattern for demonstration

## Future Enhancements

### 1. Production H.264 Encoding
Replace simplified encoding with proper implementation:
```csharp
// Use FFmpeg or hardware encoder
var encoder = new FFmpegVideoEncoder();
byte[] h264Data = encoder.EncodeFrame(yuvData, width, height);
```

### 2. Native Image Marshaling
Properly marshal Image8U native pointer data:
```csharp
// Marshal native image buffer
IntPtr bufferPtr = /* get from Image8U.buf */;
byte[] imageData = new byte[totalSize];
Marshal.Copy(bufferPtr, imageData, 0, totalSize);
```

### 3. RTP Packet Fragmentation
Implement proper RTP packetization for H.264:
```csharp
// Fragment H.264 NAL units into RTP packets
var rtpPackets = FragmentH264ToRTP(h264Frame, sequenceNumber, timestamp);
foreach (var packet in rtpPackets) {
    await rtpSender.SendPacket(packet);
}
```

### 4. Error Handling & Reconnection
- Automatic reconnection on connection failure
- Bandwidth adaptation based on network conditions
- Graceful degradation for poor connections

### 5. Multiple Stream Support
- Stream multiple sinks simultaneously
- Efficient resource management
- Dynamic stream quality adjustment

## Dependencies Added
- **SIPSorcery**: WebRTC implementation
- **SIPSorceryMedia.FFmpeg**: Media processing (for future use)
- **System.Text.Json**: JSON serialisation for API

## Testing
1. **Build the Server**: Ensure all dependencies are installed
2. **Start Server**: Run on http://localhost:8175
3. **Create Test Sink**: Add a sink with preview capability
4. **Use WebRTC Client**: Connect using JavaScript WebRTC API

## Security Considerations
- **CORS**: Configure appropriate CORS policies
- **Authentication**: Add authentication for API endpoints
- **Rate Limiting**: Implement rate limiting for stream endpoints
- **Resource Limits**: Monitor and limit concurrent streams

This implementation provides a solid foundation for WebRTC video streaming in the FRCV system, with room for enhancement based on specific production requirements.