#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/Manager.h"

class ManagerIntegrationTest : public UnitTestBase 
{
public:
    ManagerIntegrationTest();
    ~ManagerIntegrationTest();
private:
    bool innerTest() override;
    
    bool testCameraSourceWorkflow(Manager& manager);
    bool testVideoFileWorkflow(Manager& manager);
    bool testImageFileWorkflow(Manager& manager);
    bool testMultipleSinksWorkflow(Manager& manager);
    bool testSystemMonitoringWorkflow(Manager& manager);
    bool testPreviewWorkflow(Manager& manager);
    bool testCameraCalibrationWorkflow(Manager& manager);
    bool testErrorHandling(Manager& manager);
};