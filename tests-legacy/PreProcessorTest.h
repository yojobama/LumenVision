#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/PreProcessor.h"

class PreProcessorTest : public UnitTestBase {
public:
    PreProcessorTest();
    ~PreProcessorTest();
private:
    bool innerTest() override;
};