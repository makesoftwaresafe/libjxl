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

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <utility>

#include "gtest/gtest.h"
#include "jxl/aux_out.h"
#include "jxl/base/compiler_specific.h"
#include "jxl/base/data_parallel.h"
#include "jxl/base/override.h"
#include "jxl/base/padded_bytes.h"
#include "jxl/base/thread_pool_internal.h"
#include "jxl/codec_in_out.h"
#include "jxl/color_encoding.h"
#include "jxl/color_management.h"
#include "jxl/common.h"
#include "jxl/dec_file.h"
#include "jxl/dec_params.h"
#include "jxl/enc_cache.h"
#include "jxl/enc_file.h"
#include "jxl/enc_params.h"
#include "jxl/image.h"
#include "jxl/image_bundle.h"
#include "jxl/image_ops.h"

namespace jxl {
namespace {

// Returns distance of point p to line p0..p1, the result is signed and is not
// normalized.
double PointLineDist(double x0, double y0, double x1, double y1, double x,
                     double y) {
  return (y1 - y0) * x - (x1 - x0) * y + x1 * y0 - y1 * x0;
}

// Generates a test image with a gradient from one color to another.
// Angle in degrees, colors can be given in hex as 0xRRGGBB. The angle is the
// angle in which the change direction happens.
Image3F GenerateTestGradient(uint32_t color0, uint32_t color1, double angle,
                             size_t xsize, size_t ysize) {
  Image3F image(xsize, ysize);

  double x0 = xsize / 2;
  double y0 = ysize / 2;
  double x1 = x0 + std::sin(angle / 360.0 * 2.0 * kPi);
  double y1 = y0 + std::cos(angle / 360.0 * 2.0 * kPi);

  double maxdist =
      std::max<double>(fabs(PointLineDist(x0, y0, x1, y1, 0, 0)),
                       fabs(PointLineDist(x0, y0, x1, y1, xsize, 0)));

  for (size_t c = 0; c < 3; ++c) {
    float c0 = ((color0 >> (8 * (2 - c))) & 255);
    float c1 = ((color1 >> (8 * (2 - c))) & 255);
    for (size_t y = 0; y < ysize; ++y) {
      float* row = image.PlaneRow(c, y);
      for (size_t x = 0; x < xsize; ++x) {
        double dist = PointLineDist(x0, y0, x1, y1, x, y);
        double v = ((dist / maxdist) + 1.0) / 2.0;
        float color = c0 * (1.0 - v) + c1 * v;
        row[x] = color;
      }
    }
  }

  return image;
}

// Computes the max of the horizontal and vertical second derivative for each
// pixel, where second derivative means absolute value of difference of left
// delta and right delta (top/bottom for vertical direction).
// The radius over which the derivative is computed is only 1 pixel and it only
// checks two angles (hor and ver), but this approximation works well enough.
static ImageF Gradient2(const ImageF& image) {
  size_t xsize = image.xsize();
  size_t ysize = image.ysize();
  ImageF image2(image.xsize(), image.ysize());
  for (size_t y = 1; y + 1 < ysize; y++) {
    const auto* JXL_RESTRICT row0 = image.Row(y - 1);
    const auto* JXL_RESTRICT row1 = image.Row(y);
    const auto* JXL_RESTRICT row2 = image.Row(y + 1);
    auto* row_out = image2.Row(y);
    for (int x = 1; x + 1 < xsize; x++) {
      float ddx = (row1[x] - row1[x - 1]) - (row1[x + 1] - row1[x]);
      float ddy = (row1[x] - row0[x]) - (row2[x] - row1[x]);
      row_out[x] = std::max(fabsf(ddx), fabsf(ddy));
    }
  }
  // Copy to the borders
  if (ysize > 2) {
    auto* JXL_RESTRICT row0 = image2.Row(0);
    const auto* JXL_RESTRICT row1 = image2.Row(1);
    const auto* JXL_RESTRICT row2 = image2.Row(ysize - 2);
    auto* JXL_RESTRICT row3 = image2.Row(ysize - 1);
    for (size_t x = 1; x + 1 < xsize; x++) {
      row0[x] = row1[x];
      row3[x] = row2[x];
    }
  } else {
    const auto* row0_in = image.Row(0);
    const auto* row1_in = image.Row(ysize - 1);
    auto* row0_out = image2.Row(0);
    auto* row1_out = image2.Row(ysize - 1);
    for (size_t x = 1; x + 1 < xsize; x++) {
      // Image too narrow, take first derivative instead
      row0_out[x] = row1_out[x] = fabsf(row0_in[x] - row1_in[x]);
    }
  }
  if (xsize > 2) {
    for (size_t y = 0; y < ysize; y++) {
      auto* row = image2.Row(y);
      row[0] = row[1];
      row[xsize - 1] = row[xsize - 2];
    }
  } else {
    for (size_t y = 0; y < ysize; y++) {
      const auto* JXL_RESTRICT row_in = image.Row(y);
      auto* row_out = image2.Row(y);
      // Image too narrow, take first derivative instead
      row_out[0] = row_out[xsize - 1] = fabsf(row_in[0] - row_in[xsize - 1]);
    }
  }
  return image2;
}

static Image3F Gradient2(const Image3F& image) {
  return Image3F(Gradient2(image.Plane(0)), Gradient2(image.Plane(1)),
                 Gradient2(image.Plane(2)));
}

/*
Tests if roundtrip with jxl on a gradient image doesn't cause banding.
Only tests if use_gradient is true. Set to false for debugging to see the
distance values.
Angle in degrees, colors can be given in hex as 0xRRGGBB.
*/
void TestGradient(ThreadPool* pool, uint32_t color0, uint32_t color1,
                  size_t xsize, size_t ysize, float angle, bool fast_mode,
                  float butteraugli_distance, bool use_gradient = true) {
  CompressParams cparams;
  cparams.butteraugli_distance = butteraugli_distance;
  if (fast_mode) {
    cparams.speed_tier = SpeedTier::kSquirrel;
  }
  DecompressParams dparams;

  Image3F gradient = GenerateTestGradient(color0, color1, angle, xsize, ysize);

  CodecInOut io;
  io.metadata.bits_per_sample = 8;
  io.metadata.color_encoding = ColorManagement::SRGB();
  io.SetFromImage(std::move(gradient), io.metadata.color_encoding);

  CodecInOut io2;

  PaddedBytes compressed;
  AuxOut* aux_out = nullptr;
  PassesEncoderState enc_state;
  EXPECT_TRUE(EncodeFile(cparams, &io, &enc_state, &compressed, aux_out, pool));
  EXPECT_TRUE(DecodeFile(dparams, compressed, &io2, aux_out, pool));
  EXPECT_TRUE(io2.Main().TransformTo(io2.metadata.color_encoding, pool));

  if (use_gradient) {
    // Test that the gradient map worked. For that, we take a second derivative
    // of the image with Gradient2 to measure how linear the change is in x and
    // y direction. For a well handled gradient, we expect max values around
    // 0.1, while if there is noticeable banding, which means the gradient map
    // failed, the values are around 0.5-1.0 (regardless of
    // butteraugli_distance).
    Image3F gradient2 = Gradient2(io2.Main().color());

    std::array<float, 3> image_max;
    Image3Max(gradient2, &image_max);

    // TODO(jyrki): These values used to work with 0.2, 0.2, 0.2.
    EXPECT_LE(image_max[0], 3.15);
    EXPECT_LE(image_max[1], 1.72);
    EXPECT_LE(image_max[2], 5.05);
  }
}

static constexpr bool fast_mode = true;

TEST(GradientTest, SteepGradient) {
  ThreadPoolInternal pool(8);
  // Relatively steep gradients, colors from the sky of stp.png
  TestGradient(&pool, 0xd99d58, 0x889ab1, 512, 512, 90, fast_mode, 3.0);
}

TEST(GradientTest, SubtleGradient) {
  ThreadPoolInternal pool(8);
  // Very subtle gradient
  TestGradient(&pool, 0xb89b7b, 0xa89b8d, 512, 512, 90, fast_mode, 4.0);
}

}  // namespace
}  // namespace jxl
