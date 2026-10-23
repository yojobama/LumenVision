# Video/Image Upload Enhancement Updates

This document describes the comprehensive updates made to the video and image upload functionality in the React web interface to work with the new HTTP endpoints.

## Updated Files

### 1. `src/services/ApiService.ts`
- **Updated endpoint paths** to match actual C# controller routes:
  - Video files: `/api/videoFileSource/getAll`, `/api/videoFileSource/create`, `/api/videoFileSource/changeFPS`
  - Image files: `/api/imageFileSource/get`, `/api/imageFileSource/create`
  - Camera sources: `/api/cameraSource/getRegistered`, `/api/cameraSource/getNotRegistered`, `/api/cameraSource/create`, `/api/cameraSource/createAll`

- **Added enhanced upload methods**:
  - `uploadVideoFiles()` - Handles multiple video files with progress tracking
  - `uploadImageFiles()` - Handles multiple image files with progress tracking
  - `validateVideoFile()` - Client-side validation for video files
  - `validateImageFile()` - Client-side validation for image files

- **Improved error handling** and response formatting for better user feedback

### 2. `src/components/AddSourceModal.tsx`
- **Enhanced file selection** with support for multiple files
- **Real-time file validation** with immediate feedback
- **File preview functionality** showing selected files with sizes
- **Individual file removal** capability
- **Better error messaging** with specific validation details
- **Improved UX** with drag-and-drop hints and progress indicators

### 3. `src/components/BulkUploadComponent.tsx` (New)
- **Drag-and-drop upload zone** for bulk file operations
- **Automatic file categorisation** (videos vs images)
- **Progress tracking** for upload operations
- **Real-time validation** with detailed error reporting
- **Mixed file type support** (can upload videos and images simultaneously)
- **Upload results display** with success/failure indicators

### 4. `src/hooks/useAppData.ts`
- **Enhanced upload handlers** using new API methods
- **Bulk upload support** with `handleBulkVideoUpload` and `handleBulkImageUpload`
- **Better error handling** and user feedback
- **Improved data refreshing** after successful uploads

### 5. `src/App.tsx`
- **Integrated bulk upload component** in the Sources page
- **Enhanced source display** showing file paths and FPS information
- **Updated props passing** for bulk upload functionality

## Key Features Added

### File Validation
- **Video files**: MP4, AVI, MOV, WMV, MKV, WebM (max 500MB each)
- **Image files**: JPEG, PNG, BMP, GIF, WebP (max 50MB each)
- **File count limits**: Maximum 10 files per upload operation
- **Real-time validation** with immediate feedback

### Upload Capabilities
- **Multiple file selection** through file picker
- **Drag-and-drop upload** for bulk operations
- **Mixed file type uploads** (videos and images together)
- **Progress tracking** with visual indicators
- **Individual file removal** from selection

### User Experience
- **Visual feedback** for validation errors and success states
- **File preview** with names and sizes
- **Upload progress** with percentage indicators
- **Results summary** with detailed success/failure information
- **Auto-refresh** of source list after successful uploads

## API Endpoint Mapping

| Operation | Endpoint | Method | Description |
|-----------|----------|---------|-------------|
| Get video sources | `/api/videoFileSource/getAll` | GET | Retrieve all video file sources |
| Create video sources | `/api/videoFileSource/create` | POST | Upload multiple video files |
| Change video FPS | `/api/videoFileSource/changeFPS` | PATCH | Modify FPS for video source |
| Get image sources | `/api/imageFileSource/get` | GET | Retrieve all image file sources |
| Create image sources | `/api/imageFileSource/create` | POST | Upload multiple image files |
| Get registered cameras | `/api/cameraSource/getRegistered` | GET | Get currently registered cameras |
| Get available cameras | `/api/cameraSource/getNotRegistered` | GET | Get unregistered cameras |
| Create camera source | `/api/cameraSource/create` | POST | Create single camera source |
| Create all cameras | `/api/cameraSource/createAll` | POST | Create sources for all cameras |

## File Upload Flow

1. **File Selection**: User selects files via modal or drag-and-drop
2. **Client Validation**: Files validated for type, size, and count
3. **Upload Preparation**: Valid files categorised by type
4. **Server Upload**: Files sent to appropriate endpoints
5. **Progress Tracking**: Visual feedback during upload
6. **Result Display**: Success/failure feedback with details
7. **Data Refresh**: Source list updated with new entries

## Error Handling

### Client-Side Validation
- File type verification
- File size limits
- File count restrictions
- Real-time feedback

### Server-Side Error Handling
- Network error detection
- HTTP status code handling
- Detailed error messaging
- Graceful failure recovery

## Performance Optimisations

- **Chunked uploads**: Large files handled efficiently
- **Progress tracking**: Real-time upload status
- **Parallel processing**: Videos and images uploaded simultaneously
- **Local validation**: Reduces server load
- **Auto-retry**: Failed uploads can be retried

## Future Enhancements

When additional features are implemented:

1. **Upload resumption** for interrupted transfers
2. **Thumbnail generation** for uploaded videos/images
3. **Metadata extraction** (resolution, duration, etc.)
4. **Batch processing** with custom settings per file
5. **Cloud storage integration** for large files
6. **Advanced file format support** (RAW images, additional codecs)

## Testing Notes

- Multiple file uploads work correctly
- File validation prevents invalid uploads
- Progress tracking provides accurate feedback
- Error handling is comprehensive
- UI remains responsive during uploads
- Sources list updates automatically after uploads

## Browser Compatibility

- **Drag-and-drop**: Supported in all modern browsers
- **File API**: Full support for FileList and File objects
- **Progress tracking**: Uses standard web APIs
- **FormData**: Native multipart upload support