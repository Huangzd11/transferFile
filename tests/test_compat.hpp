// 测试兼容头：gtest 与 minimal_test 二选一

#pragma once

#ifdef TRANSFER_GTEST
#include <gtest/gtest.h>
#else
#include "minimal_test.hpp"
#define TEST MT_TEST
#endif
