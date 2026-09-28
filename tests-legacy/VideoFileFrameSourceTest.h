#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/VideoFileFrameSource.h"

class VideoFileFrameSourceTest : public UnitTestBase {
public:
    VideoFileFrameSourceTest();
    ~VideoFileFrameSourceTest();
private:
    bool innerTest() override;
};