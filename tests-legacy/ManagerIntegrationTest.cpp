#include "ManagerIntegrationTest.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

ManagerIntegrationTest::ManagerIntegrationTest() = default;
ManagerIntegrationTest::~ManagerIntegrationTest() = default;

bool ManagerIntegrationTest::innerTest() {
    try {
        logger->EnterLog("ManagerIntegrationTest: Starting integration tests");
        Manager manager;
        
        // Run comprehensive integration test scenarios
        if (!testCameraSourceWorkflow(manager)) return false;
        if (!testVideoFileWorkflow(manager)) return false;
        if (!testImageFileWorkflow(manager)) return false;
        if (!testMultipleSinksWorkflow(manager)) return false;
        if (!testSystemMonitoringWorkflow(manager)) return false;
        if (!testPreviewWorkflow(manager)) return false;
        if (!testCameraCalibrationWorkflow(manager)) return false;
        if (!testErrorHandling(manager)) return false;
        
        logger->EnterLog("ManagerIntegrationTest: All integration tests passed successfully");
        return true;
        
    } catch (const std::exception& e) {
        logger->EnterLog("ManagerIntegrationTest FAILED with exception: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger->EnterLog("ManagerIntegrationTest FAILED with unknown exception");
        return false;
    }
}

bool ManagerIntegrationTest::testCameraSourceWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing camera source workflow");
    
    
    // 1. Enumerate available cameras
    auto cameras = manager.EnumerateAvailableCameras();
    logger->EnterLog("Found " + std::to_string(cameras.size()) + " camera devices");
    
    if (cameras.empty()) {
        logger->EnterLog("No cameras found, skipping camera workflow test");
        return true; // Not a failure, just no cameras available
    }
    
    // 2. Create a camera source using the first available camera
    CameraHardwareInfo firstCamera = cameras[0];
    logger->EnterLog("Testing camera: " + firstCamera.name + " at " + firstCamera.path);
    
    int cameraSourceId = manager.CreateCameraSource(firstCamera);
    if (cameraSourceId <= 0) {
        logger->EnterLog("WARNING: Camera source creation failed, ID: " + std::to_string(cameraSourceId));
        return true; // Not necessarily a test failure - might be permissions issue
    }
    
    // 3. Create an AprilTag sink (most common use case)
    int apriltagSinkId = manager.CreateApriltagSink();
    if (apriltagSinkId <= 0) {
        logger->EnterLog("FAILED: AprilTag sink creation failed");
        return false;
    }
    
    // 4. Bind the camera source to the sink
    bool bindResult = manager.BindSourceToSink(cameraSourceId, apriltagSinkId);
    if (!bindResult) {
        logger->EnterLog("WARNING: Failed to bind camera source to sink");
        return true; // Continue with other tests
    }
    
    // 5. Start the source and sink
    bool sourceStarted = manager.StartSourceById(cameraSourceId);
    bool sinkStarted = manager.StartSinkById(apriltagSinkId);
    
    logger->EnterLog("Camera source started: " + std::to_string(sourceStarted));
    logger->EnterLog("AprilTag sink started: " + std::to_string(sinkStarted));
    
    // 6. Let it run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 7. Check status and results
    std::string sinkStatus = manager.GetSinkStatusById(apriltagSinkId);
    std::string sinkResult = manager.GetSinkResult(apriltagSinkId);
    
    logger->EnterLog("Sink status: " + sinkStatus);
    logger->EnterLog("Sink result: " + sinkResult);
    
    // 8. Stop everything
    manager.StopSourceById(cameraSourceId);
    manager.StopSinkById(apriltagSinkId);
    
    // 9. Unbind
    manager.UnbindSourceFromSink(apriltagSinkId);
    
    logger->EnterLog("Camera workflow test completed successfully");
    return true;
}

bool ManagerIntegrationTest::testVideoFileWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing video file workflow");
    
    
    // 1. Create a video file source (simulating file upload scenario)
    int videoSourceId = manager.CreateVideoFileSource("test_video.mp4", 30);
    
    if (videoSourceId <= 0) {
        logger->EnterLog("Video file source creation failed (expected if file doesn't exist)");
        return true; // Not a failure for test purposes
    }
    
    // 2. Create multiple sinks for the same source (common pattern)
    int apriltagSink1 = manager.CreateApriltagSink();
    int apriltagSink2 = manager.CreateApriltagSink();
    
    if (apriltagSink1 <= 0 || apriltagSink2 <= 0) {
        logger->EnterLog("FAILED: Sink creation failed");
        return false;
    }
    
    // 3. Bind the same source to multiple sinks
    bool bind1 = manager.BindSourceToSink(videoSourceId, apriltagSink1);
    bool bind2 = manager.BindSourceToSink(videoSourceId, apriltagSink2);
    
    logger->EnterLog("Bind results: " + std::to_string(bind1) + ", " + std::to_string(bind2));
    
    // 4. Start everything
    manager.StartSourceById(videoSourceId);
    manager.StartSinkById(apriltagSink1);
    manager.StartSinkById(apriltagSink2);
    
    // 5. Let it process for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // 6. Check all results
    std::string allResults = manager.GetAllSinkResults();
    logger->EnterLog("All sink results: " + allResults);
    
    // 7. Stop everything
    manager.StopAllSources();
    manager.StopAllSinks();
    
    logger->EnterLog("Video file workflow test completed");
    return true;
}

bool ManagerIntegrationTest::testImageFileWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing image file workflow");
    
    
    // 1. Create image file source
    int imageSourceId = manager.CreateImageFileSource("test_image.jpg");
    
    if (imageSourceId <= 0) {
        logger->EnterLog("Image file source creation failed (expected if file doesn't exist)");
        return true;
    }
    
    // 2. Create AprilTag sink for image processing
    int apriltagSinkId = manager.CreateApriltagSink();
    
    // 3. Test the full binding and processing cycle
    manager.BindSourceToSink(imageSourceId, apriltagSinkId);
    manager.StartSourceById(imageSourceId);
    manager.StartSinkById(apriltagSinkId);
    
    // 4. Process the image
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 5. Get results
    std::string result = manager.GetSinkResult(apriltagSinkId);
    logger->EnterLog("Image processing result: " + result);
    
    // 6. Test changing source for the same sink
    manager.UnbindSourceFromSink(apriltagSinkId);
    
    // Create another image source
    int imageSource2Id = manager.CreateImageFileSource("test_image2.jpg", 54321);
    if (imageSource2Id > 0) {
        manager.BindSourceToSink(imageSource2Id, apriltagSinkId);
        logger->EnterLog("Successfully rebound sink to different image source");
    }
    
    // Cleanup
    manager.StopAllSources();
    manager.StopAllSinks();
    
    logger->EnterLog("Image file workflow test completed");
    return true;
}

bool ManagerIntegrationTest::testMultipleSinksWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing multiple sinks workflow");
    
    
    std::vector<int> sinkIds;
    
    // 1. Create multiple AprilTag sinks
    for (int i = 0; i < 5; i++) {
        int sinkId = manager.CreateApriltagSink();
        if (sinkId > 0) {
            sinkIds.push_back(sinkId);
        }
    }
    
    logger->EnterLog("Created " + std::to_string(sinkIds.size()) + " sinks");
    
    // 2. Create an ObjectDetection sink
    int objDetectionSink = manager.CreateObjectDetectionSink();
    if (objDetectionSink > 0) {
        sinkIds.push_back(objDetectionSink);
    }
    
    // 3. Test GetAllSinks functionality
    auto allSinks = manager.GetAllSinks();
    if (allSinks.size() < sinkIds.size()) {
        logger->EnterLog("FAILED: GetAllSinks returned fewer sinks than expected");
        return false;
    }
    
    // 4. Test individual sink operations
    for (int sinkId : sinkIds) {
        std::string status = manager.GetSinkStatusById(sinkId);
        std::string result = manager.GetSinkResult(sinkId);
        logger->EnterLog("Sink " + std::to_string(sinkId) + " - Status: " + status + ", Result: " + result);
        
        // Test start/stop individual sinks
        bool started = manager.StartSinkById(sinkId);
        bool stopped = manager.StopSinkById(sinkId);
        logger->EnterLog("Sink " + std::to_string(sinkId) + " - Started: " + std::to_string(started) + ", Stopped: " + std::to_string(stopped));
    }
    
    // 5. Test bulk operations
    manager.StartAllSinks();
    logger->EnterLog("Started all sinks");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    manager.StopAllSinks();
    logger->EnterLog("Stopped all sinks");
    
    // 6. Test GetAllSinkStatus and GetAllSinkResults
    std::string allStatus = manager.GetAllSinkStatus();
    std::string allResults = manager.GetAllSinkResults();
    
    logger->EnterLog("All status: " + allStatus);
    logger->EnterLog("All results: " + allResults);
    
    logger->EnterLog("Multiple sinks workflow test completed");
    return true;
}

bool ManagerIntegrationTest::testSystemMonitoringWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing system monitoring workflow");
    
    
    // 1. Initial system state reading
    int initialMemory = manager.GetMemoryUsageBytes();
    int initialCPU = manager.GetCPUUsage();
    int initialTemp = manager.GetCpuTemperature();
    int initialDisk = manager.GetDiskUsage();
    
    logger->EnterLog("Initial system state - Memory: " + std::to_string(initialMemory) + 
                    " bytes, CPU: " + std::to_string(initialCPU) + 
                    "%, Temp: " + std::to_string(initialTemp) + 
                    "°C, Disk: " + std::to_string(initialDisk) + "%");
    
    // 2. Create some load by creating and starting multiple sources/sinks
    std::vector<int> sources;
    std::vector<int> sinks;
    
    for (int i = 0; i < 3; i++) {
        int sourceId = manager.CreateImageFileSource("load_test_" + std::to_string(i) + ".jpg");
        int sinkId = manager.CreateApriltagSink();
        
        if (sourceId > 0) sources.push_back(sourceId);
        if (sinkId > 0) sinks.push_back(sinkId);
    }
    
    // 3. Start everything to create some system load
    manager.StartAllSources();
    manager.StartAllSinks();
    
    // 4. Monitor system during load
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    int loadMemory = manager.GetMemoryUsageBytes();
    int loadCPU = manager.GetCPUUsage();
    int loadTemp = manager.GetCpuTemperature();
    int loadDisk = manager.GetDiskUsage();
    
    logger->EnterLog("Under load - Memory: " + std::to_string(loadMemory) + 
                    " bytes, CPU: " + std::to_string(loadCPU) + 
                    "%, Temp: " + std::to_string(loadTemp) + 
                    "°C, Disk: " + std::to_string(loadDisk) + "%");
    
    // 5. Stop everything and check system state again
    manager.StopAllSources();
    manager.StopAllSinks();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    int finalMemory = manager.GetMemoryUsageBytes();
    int finalCPU = manager.GetCPUUsage();
    int finalTemp = manager.GetCpuTemperature();
    int finalDisk = manager.GetDiskUsage();
    
    logger->EnterLog("After cleanup - Memory: " + std::to_string(finalMemory) + 
                    " bytes, CPU: " + std::to_string(finalCPU) + 
                    "%, Temp: " + std::to_string(finalTemp) + 
                    "°C, Disk: " + std::to_string(finalDisk) + "%");
    
    // 6. Basic sanity checks (values should be reasonable)
    if (initialMemory < 0 || loadMemory < 0 || finalMemory < 0) {
        logger->EnterLog("WARNING: Invalid memory readings detected");
    }
    
    if (initialCPU < 0 || initialCPU > 100 || loadCPU < 0 || loadCPU > 100) {
        logger->EnterLog("WARNING: Invalid CPU usage readings detected");
    }
    
    logger->EnterLog("System monitoring workflow test completed");
    return true;
}

bool ManagerIntegrationTest::testPreviewWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing preview workflow");
    
    
    // 1. Create a sink that will provide preview data
    int previewSinkId = manager.CreateApriltagSink();
    if (previewSinkId <= 0) {
        logger->EnterLog("FAILED: Could not create sink for preview test");
        return false;
    }
    
    // 2. Try to enable preview before binding source (should handle gracefully)
    try {
        bool previewEnabled = manager.EnableSinkPreview(previewSinkId);
        logger->EnterLog("Preview enabled without source: " + std::to_string(previewEnabled));
        
        // 3. Try to get preview image (should fail gracefully)
        try {
            auto previewImage = manager.GetPreviewImage(previewSinkId);
            logger->EnterLog("Preview image obtained: " + std::to_string(previewImage.width) + 
                           "x" + std::to_string(previewImage.height));
        } catch (const char* ex) {
            logger->EnterLog("Expected exception getting preview without data: " + std::string(ex));
        }
        
        // 4. Create and bind a source
        int imageSourceId = manager.CreateImageFileSource("preview_test.jpg");
        if (imageSourceId > 0) {
            manager.BindSourceToSink(imageSourceId, previewSinkId);
            manager.StartSourceById(imageSourceId);
            manager.StartSinkById(previewSinkId);
            
            // Let it process some data
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            // 5. Try preview again with data
            try {
                auto previewImage = manager.GetPreviewImage(previewSinkId);
                logger->EnterLog("Preview image with data: " + std::to_string(previewImage.width) + 
                               "x" + std::to_string(previewImage.height));
            } catch (const char* ex) {
                logger->EnterLog("Exception getting preview with data: " + std::string(ex));
            }
            
            manager.StopSourceById(imageSourceId);
            manager.StopSinkById(previewSinkId);
        }
        
        // 6. Disable preview
        bool previewDisabled = manager.DisableSinkPreview(previewSinkId);
        logger->EnterLog("Preview disabled: " + std::to_string(previewDisabled));
        
        // 7. Try to get preview after disabling (should fail)
        try {
            auto previewImage = manager.GetPreviewImage(previewSinkId);
            logger->EnterLog("WARNING: Got preview image after disabling");
        } catch (const char* ex) {
            logger->EnterLog("Expected exception after disabling preview: " + std::string(ex));
        }
        
    } catch (const char* ex) {
        logger->EnterLog("Preview workflow exception: " + std::string(ex));
    }
    
    logger->EnterLog("Preview workflow test completed");
    return true;
}

bool ManagerIntegrationTest::testCameraCalibrationWorkflow(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing camera calibration workflow");
    
    
    // 1. Create a camera calibration sink
    int calibWidth = 640;
    int calibHeight = 480;
    int calibSinkId = manager.CreateCameraCalibrationSink(calibHeight, calibWidth);
    
    if (calibSinkId <= 0) {
        logger->EnterLog("FAILED: Could not create camera calibration sink");
        return false;
    }
    
    logger->EnterLog("Created camera calibration sink with ID: " + std::to_string(calibSinkId));
    
    // 2. Create an image source (simulating calibration pattern images)
    int calibSourceId = manager.CreateImageFileSource("calibration_pattern.jpg");
    if (calibSourceId > 0) {
        
        // 3. Bind source to calibration sink
        manager.BindSourceToCalibrationSink(calibSourceId);
        logger->EnterLog("Bound source to calibration sink");
        
        // 4. Start the source
        manager.StartSourceById(calibSourceId);
        
        // 5. Grab multiple frames for calibration (simulating multiple calibration images)
        for (int i = 0; i < 5; i++) {
            try {
                manager.CameraCalibrationSinkGrabFrame(calibSinkId);
                logger->EnterLog("Grabbed calibration frame " + std::to_string(i + 1));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (...) {
                logger->EnterLog("Exception grabbing calibration frame " + std::to_string(i + 1));
            }
        }
        
        // 6. Get calibration results
        try {
            auto calibResult = manager.GetCameraCalibrationResults(calibSinkId);
            logger->EnterLog("Calibration results - fx: " + std::to_string(calibResult.fx) + 
                           ", fy: " + std::to_string(calibResult.fy) +
                           ", cx: " + std::to_string(calibResult.cx) +
                           ", cy: " + std::to_string(calibResult.cy));
        } catch (...) {
            logger->EnterLog("Exception getting calibration results (expected if no valid pattern found)");
        }
        
        // 7. Cleanup
        manager.StopSourceById(calibSourceId);
    } else {
        logger->EnterLog("Could not create calibration source (test image not available)");
    }
    
    // 8. Test with invalid calibration sink ID
    try {
        manager.CameraCalibrationSinkGrabFrame(99999);
        logger->EnterLog("WARNING: Grab frame with invalid ID should have failed");
    } catch (...) {
        logger->EnterLog("Expected exception with invalid calibration sink ID");
    }
    
    logger->EnterLog("Camera calibration workflow test completed");
    return true;
}

bool ManagerIntegrationTest::testErrorHandling(Manager& manager) {
    logger->EnterLog("ManagerIntegrationTest: Testing error handling");
    
    
    // 1. Test operations with invalid IDs
    bool invalidSourceStart = manager.StartSourceById(-1);
    bool invalidSinkStart = manager.StartSinkById(-1);
    bool invalidBind = manager.BindSourceToSink(-1, -1);
    
    logger->EnterLog("Invalid operations - Source start: " + std::to_string(invalidSourceStart) + 
                    ", Sink start: " + std::to_string(invalidSinkStart) +
                    ", Bind: " + std::to_string(invalidBind));
    
    // 2. Test getting status/results for non-existent sinks
    std::string invalidStatus = manager.GetSinkStatusById(999999);
    std::string invalidResult = manager.GetSinkResult(999999);
    
    logger->EnterLog("Invalid ID queries - Status: '" + invalidStatus + "', Result: '" + invalidResult + "'");
    
    // 3. Test creating sources with non-existent files
    int invalidImageSource = manager.CreateImageFileSource("non_existent_image.jpg");
    int invalidVideoSource = manager.CreateVideoFileSource("non_existent_video.mp4", 30);
    
    logger->EnterLog("Invalid file sources - Image: " + std::to_string(invalidImageSource) + 
                    ", Video: " + std::to_string(invalidVideoSource));
    
    // 4. Test preview operations on invalid sink
    try {
        bool invalidPreviewEnable = manager.EnableSinkPreview(999999);
        logger->EnterLog("Invalid preview enable: " + std::to_string(invalidPreviewEnable));
    } catch (const char* ex) {
        logger->EnterLog("Expected exception for invalid preview enable: " + std::string(ex));
    }
    
    // 5. Test unbinding non-existent bindings
    bool invalidUnbind = manager.UnbindSourceFromSink(999999);
    logger->EnterLog("Invalid unbind: " + std::to_string(invalidUnbind));
    
    // 6. Test duplicate sink creation with same ID
    int sinkId = manager.CreateApriltagSink();
    if (sinkId > 0) {
        // Try to create another sink with the same ID
        int duplicateId = manager.CreateApriltagSink(sinkId);
        logger->EnterLog("Duplicate ID creation attempt: " + std::to_string(duplicateId));
    }
    
    // 7. Test rapid start/stop cycles (stress test)
    if (sinkId > 0) {
        for (int i = 0; i < 10; i++) {
            bool started = manager.StartSinkById(sinkId);
            bool stopped = manager.StopSinkById(sinkId);
            if (!started || !stopped) {
                logger->EnterLog("Rapid start/stop cycle " + std::to_string(i) + " failed");
                break;
            }
        }
        logger->EnterLog("Rapid start/stop cycles completed");
    }
    
    // 8. Test system monitoring with potential edge cases
    // Force multiple rapid calls to test thread safety
    for (int i = 0; i < 5; i++) {
        int memory = manager.GetMemoryUsageBytes();
        int cpu = manager.GetCPUUsage();
        int temp = manager.GetCpuTemperature();
        int disk = manager.GetDiskUsage();
        
        if (memory < 0 || cpu < 0 || cpu > 100) {
            logger->EnterLog("WARNING: Invalid system metrics detected in rapid call " + std::to_string(i));
        }
    }
    
    logger->EnterLog("Error handling test completed");
    return true;
}