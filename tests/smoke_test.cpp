#include <gtest/gtest.h>

#include "ftx/version.hpp"

namespace {

TEST(Smoke, VersionIsNonEmpty) {
    EXPECT_FALSE(ftx::version().empty());
}

TEST(Smoke, VersionMatchesConstant) {
    EXPECT_EQ(ftx::version(), ftx::kVersion);
}

}  // namespace
