#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/CameraFrameSource.h"

class CameraFrameSourceTest : public UnitTestBase {
public:
    CameraFrameSourceTest();
    ~CameraFrameSourceTest();
private:
    bool innerTest() override;
};