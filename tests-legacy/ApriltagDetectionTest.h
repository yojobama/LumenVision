#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/ApriltagDetection.h"

class ApriltagDetectionTest : public UnitTestBase {
public:
    ApriltagDetectionTest();
    ~ApriltagDetectionTest();
private:
    bool innerTest() override;
};