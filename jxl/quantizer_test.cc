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

#include "jxl/quantizer.h"

#include <random>

#include "gtest/gtest.h"
#include "jxl/base/span.h"
#include "jxl/common.h"
#include "jxl/dec_bit_reader.h"
#include "jxl/image_ops.h"
#include "jxl/image_test_utils.h"

namespace jxl {
namespace {

void TestEquivalence(int qxsize, int qysize, const Quantizer& quantizer1,
                     const Quantizer& quantizer2) {
  ASSERT_NEAR(quantizer1.inv_quant_dc(), quantizer2.inv_quant_dc(), 1e-7);
}

TEST(QuantizerTest, QuantizerParams) { TestQuantizerParams(); }

TEST(QuantizerTest, BitStreamRoundtripSameQuant) {
  const int qxsize = 8;
  const int qysize = 8;
  DequantMatrices dequant;
  Quantizer quantizer1(&dequant);
  ImageI raw_quant_field(qxsize, qysize);
  quantizer1.SetQuant(0.17f, 0.17f, &raw_quant_field);
  BitWriter writer;
  EXPECT_TRUE(quantizer1.Encode(&writer, 0, nullptr));
  writer.ZeroPadToByte();
  const size_t bits_written = writer.BitsWritten();
  Quantizer quantizer2(&dequant, qxsize, qysize);
  BitReader reader(writer.GetSpan());
  EXPECT_TRUE(quantizer2.Decode(&reader));
  EXPECT_TRUE(reader.JumpToByteBoundary());
  EXPECT_EQ(reader.TotalBitsConsumed(), bits_written);
  EXPECT_TRUE(reader.Close());
  TestEquivalence(qxsize, qysize, quantizer1, quantizer2);
}

TEST(QuantizerTest, BitStreamRoundtripRandomQuant) {
  const int qxsize = 8;
  const int qysize = 8;
  DequantMatrices dequant;
  Quantizer quantizer1(&dequant);
  ImageI raw_quant_field(qxsize, qysize);
  quantizer1.SetQuant(0.17f, 0.17f, &raw_quant_field);
  std::mt19937_64 rng;
  std::uniform_int_distribution<> uniform(1, 256);
  float quant_dc = 0.17f;
  ImageF qf(qxsize, qysize);
  RandomFillImage(&qf, 1.0f);
  quantizer1.SetQuantField(quant_dc, qf, &raw_quant_field);
  BitWriter writer;
  EXPECT_TRUE(quantizer1.Encode(&writer, 0, nullptr));
  writer.ZeroPadToByte();
  const size_t bits_written = writer.BitsWritten();
  Quantizer quantizer2(&dequant, qxsize, qysize);
  BitReader reader(writer.GetSpan());
  EXPECT_TRUE(quantizer2.Decode(&reader));
  EXPECT_TRUE(reader.JumpToByteBoundary());
  EXPECT_EQ(reader.TotalBitsConsumed(), bits_written);
  EXPECT_TRUE(reader.Close());
  TestEquivalence(qxsize, qysize, quantizer1, quantizer2);
}
}  // namespace
}  // namespace jxl
