#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/Frame.h"

class FrameTest : public UnitTestBase {
public:
    FrameTest();
    ~FrameTest();
private:
    bool innerTest() override;
};