// Copyright (c) the JPEG XL Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "jxl/base/bits.h"

#include "gtest/gtest.h"

namespace jxl {
namespace {

TEST(BitsTest, TestPopCount) {
  EXPECT_EQ(0, PopCount(0u));
  EXPECT_EQ(1, PopCount(1u));
  EXPECT_EQ(1, PopCount(2u));
  EXPECT_EQ(2, PopCount(3u));
  EXPECT_EQ(1, PopCount(0x80000000u));
  EXPECT_EQ(31, PopCount(0x7FFFFFFFu));
  EXPECT_EQ(32, PopCount(0xFFFFFFFFu));

  EXPECT_EQ(1, PopCount(0x80000000ull));
  EXPECT_EQ(31, PopCount(0x7FFFFFFFull));
  EXPECT_EQ(32, PopCount(0xFFFFFFFFull));
  EXPECT_EQ(33, PopCount(0x10FFFFFFFFull));
  EXPECT_EQ(63, PopCount(0xFFFEFFFFFFFFFFFFull));
  EXPECT_EQ(64, PopCount(0xFFFFFFFFFFFFFFFFull));
}

TEST(BitsTest, TestNumZeroBits) {
  // Zero input is well-defined.
  EXPECT_EQ(32, NumZeroBitsAboveMSB(0u));
  EXPECT_EQ(64, NumZeroBitsAboveMSB(0ull));
  EXPECT_EQ(32, NumZeroBitsBelowLSB(0u));
  EXPECT_EQ(64, NumZeroBitsBelowLSB(0ull));

  EXPECT_EQ(31, NumZeroBitsAboveMSB(1u));
  EXPECT_EQ(30, NumZeroBitsAboveMSB(2u));
  EXPECT_EQ(63, NumZeroBitsAboveMSB(1ull));
  EXPECT_EQ(62, NumZeroBitsAboveMSB(2ull));

  EXPECT_EQ(0, NumZeroBitsBelowLSB(1u));
  EXPECT_EQ(0, NumZeroBitsBelowLSB(1ull));
  EXPECT_EQ(1, NumZeroBitsBelowLSB(2u));
  EXPECT_EQ(1, NumZeroBitsBelowLSB(2ull));

  EXPECT_EQ(0, NumZeroBitsAboveMSB(0x80000000u));
  EXPECT_EQ(0, NumZeroBitsAboveMSB(0x8000000000000000ull));
  EXPECT_EQ(31, NumZeroBitsBelowLSB(0x80000000u));
  EXPECT_EQ(63, NumZeroBitsBelowLSB(0x8000000000000000ull));
}

TEST(BitsTest, TestFloorLog2) {
  // for input = [1, 7]
  const int expected[7] = {0, 1, 1, 2, 2, 2, 2};
  for (uint32_t i = 1; i <= 7; ++i) {
    EXPECT_EQ(expected[i - 1], FloorLog2Nonzero(i)) << " " << i;
    EXPECT_EQ(expected[i - 1], FloorLog2Nonzero(uint64_t(i))) << " " << i;
  }

  EXPECT_EQ(31, FloorLog2Nonzero(0x80000000u));
  EXPECT_EQ(31, FloorLog2Nonzero(0x80000001u));
  EXPECT_EQ(31, FloorLog2Nonzero(0xFFFFFFFFu));

  EXPECT_EQ(31, FloorLog2Nonzero(0x80000000ull));
  EXPECT_EQ(31, FloorLog2Nonzero(0x80000001ull));
  EXPECT_EQ(31, FloorLog2Nonzero(0xFFFFFFFFull));

  EXPECT_EQ(63, FloorLog2Nonzero(0x8000000000000000ull));
  EXPECT_EQ(63, FloorLog2Nonzero(0x8000000000000001ull));
  EXPECT_EQ(63, FloorLog2Nonzero(0xFFFFFFFFFFFFFFFFull));
}

TEST(BitsTest, TestCeilLog2) {
  // for input = [1, 7]
  const int expected[7] = {0, 1, 2, 2, 3, 3, 3};
  for (uint32_t i = 1; i <= 7; ++i) {
    EXPECT_EQ(expected[i - 1], CeilLog2Nonzero(i)) << " " << i;
    EXPECT_EQ(expected[i - 1], CeilLog2Nonzero(uint64_t(i))) << " " << i;
  }

  EXPECT_EQ(31, CeilLog2Nonzero(0x80000000u));
  EXPECT_EQ(32, CeilLog2Nonzero(0x80000001u));
  EXPECT_EQ(32, CeilLog2Nonzero(0xFFFFFFFFu));

  EXPECT_EQ(31, CeilLog2Nonzero(0x80000000ull));
  EXPECT_EQ(32, CeilLog2Nonzero(0x80000001ull));
  EXPECT_EQ(32, CeilLog2Nonzero(0xFFFFFFFFull));

  EXPECT_EQ(63, CeilLog2Nonzero(0x8000000000000000ull));
  EXPECT_EQ(64, CeilLog2Nonzero(0x8000000000000001ull));
  EXPECT_EQ(64, CeilLog2Nonzero(0xFFFFFFFFFFFFFFFFull));
}

}  // namespace
}  // namespace jxl
