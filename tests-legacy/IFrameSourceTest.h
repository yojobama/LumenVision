#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/IFrameSource.h"

class IFrameSourceTest : public UnitTestBase {
public:
    IFrameSourceTest();
    ~IFrameSourceTest();
private:
    bool innerTest() override;
};