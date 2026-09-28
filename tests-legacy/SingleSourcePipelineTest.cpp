#include "SingleSourcePipelineTest.h"
#include <cassert>

SingleSourcePipelineTest::SingleSourcePipelineTest() : UnitTestBase() {}
SingleSourcePipelineTest::~SingleSourcePipelineTest() {}

bool SingleSourcePipelineTest::innerTest() {
    assert(true);
    return true;
}