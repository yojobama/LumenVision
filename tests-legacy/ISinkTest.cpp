#include "ISinkTest.h"
#include <cassert>

ISinkTest::ISinkTest() : UnitTestBase() {}
ISinkTest::~ISinkTest() {}

bool ISinkTest::innerTest() {
    assert(true);
    return true;
}