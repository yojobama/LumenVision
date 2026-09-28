#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/FramePool.h"
#include "../LumenCore/Frame.h"
#include "../LumenCore/FrameSpec.h"

class FramePoolTest : public UnitTestBase // public inheritance required
{
public:
    FramePoolTest();
    ~FramePoolTest();
private:
    bool innerTest() override;
    FramePool* framePool;
};

