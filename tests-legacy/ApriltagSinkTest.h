#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/ApriltagSink.h"
#include "../LumenCore/Frame.h"

class ApriltagSinkTest : public UnitTestBase
{
public:
    ApriltagSinkTest();
    ~ApriltagSinkTest();
private:
    bool innerTest() override;
};