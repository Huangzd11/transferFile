#pragma once

#ifdef TRANSFER_GTEST
#include <gtest/gtest.h>
#else
#include "minimal_test.hpp"
#define TEST MT_TEST
#endif
