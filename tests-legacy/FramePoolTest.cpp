#include "FramePoolTest.h"
#include <cassert>
#include <vector>
#include <iostream>

FramePoolTest::FramePoolTest() : framePool(nullptr) {
}

FramePoolTest::~FramePoolTest() {
    delete framePool;
}

bool FramePoolTest::innerTest() {
    std::vector<FrameSpec> specs;
    Logger logger;
    framePool = new FramePool(specs, &logger);

    if (framePool->getCachedFrameCount() != 0) {
        std::cerr << "FramePoolTest failed: Cached frame count is not zero." << std::endl;
        return false;
    }

    return true;
}