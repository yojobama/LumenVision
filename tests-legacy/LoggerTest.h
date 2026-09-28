#pragma once
#include "UnitTestBase.h"
#include "../LumenCore/Logger.h"

class LoggerTest : public UnitTestBase {
public:
    LoggerTest();
    ~LoggerTest();
private:
    bool innerTest() override;
};