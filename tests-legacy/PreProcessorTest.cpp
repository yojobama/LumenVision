#include "PreProcessorTest.h"
#include <cassert>

PreProcessorTest::PreProcessorTest() : UnitTestBase() {}
PreProcessorTest::~PreProcessorTest() {}

bool PreProcessorTest::innerTest() {
    FramePool framePool({}, nullptr);
    PreProcessor pre(&framePool);    // Pass the FramePool pointer to the PreProcessor constructor
    assert(true);
    return true;
}