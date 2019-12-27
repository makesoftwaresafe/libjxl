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

#include "jxl/headers.h"

#include "jxl/common.h"
#include "jxl/fields.h"

namespace jxl {
namespace {

struct Rational {
  constexpr explicit Rational(uint32_t num, uint32_t den)
      : num(num), den(den) {}

  // Returns floor(multiplicand * rational).
  constexpr uint32_t MulTruncate(uint32_t multiplicand) const {
    return uint64_t(multiplicand) * num / den;
  }

  uint32_t num;
  uint32_t den;
};

Rational FixedAspectRatios(uint32_t ratio) {
  JXL_ASSERT(0 != ratio && ratio < 8);
  // Other candidates: 5/4, 7/5, 14/9, 16/10, 5/3, 21/9, 12/5
  constexpr Rational kRatios[7] = {Rational(1, 1),    // square
                                   Rational(12, 10),  //
                                   Rational(4, 3),    // camera
                                   Rational(3, 2),    // mobile camera
                                   Rational(16, 9),   // camera/display
                                   Rational(5, 4),    //
                                   Rational(2, 1)};   //
  return kRatios[ratio - 1];
}

uint32_t FindAspectRatio(uint32_t xsize, uint32_t ysize) {
  for (uint32_t r = 1; r < 8; ++r) {
    if (xsize == FixedAspectRatios(r).MulTruncate(ysize)) {
      return r;
    }
  }
  return 0;  // Must send xsize instead
}

}  // namespace

size_t SizeHeader::xsize() const {
  if (ratio_ != 0) {
    return FixedAspectRatios(ratio_).MulTruncate(
        static_cast<uint32_t>(ysize()));
  }
  return small_ ? ((xsize_div8_minus_1_ + 1) * 8) : xsize_minus_1_ + 1;
}

Status SizeHeader::Set(size_t xsize64, size_t ysize64) {
  if (xsize64 > 0xFFFFFFFFull || ysize64 > 0xFFFFFFFFull) {
    return JXL_FAILURE("Image too large");
  }
  const uint32_t xsize32 = static_cast<uint32_t>(xsize64);
  const uint32_t ysize32 = static_cast<uint32_t>(ysize64);
  if (xsize64 == 0 || ysize64 == 0) return JXL_FAILURE("Empty image");
  small_ = xsize64 <= kGroupDim && ysize64 <= kGroupDim &&
           (xsize64 % kBlockDim) == 0 && (ysize64 % kBlockDim) == 0;
  if (small_) {
    ysize_div8_minus_1_ = ysize32 / 8 - 1;
  } else {
    ysize_minus_1_ = ysize32 - 1;
  }

  ratio_ = FindAspectRatio(xsize32, ysize32);
  if (ratio_ == 0) {
    if (small_) {
      xsize_div8_minus_1_ = xsize32 / 8 - 1;
    } else {
      xsize_minus_1_ = xsize32 - 1;
    }
  }
  JXL_ASSERT(xsize() == xsize64);
  JXL_ASSERT(ysize() == ysize64);
  return true;
}

Status PreviewHeader::Set(size_t xsize64, size_t ysize64) {
  const uint32_t xsize32 = static_cast<uint32_t>(xsize64);
  const uint32_t ysize32 = static_cast<uint32_t>(ysize64);
  if (xsize64 == 0 || ysize64 == 0) return JXL_FAILURE("Empty preview");
  div8_ = (xsize64 % kBlockDim) == 0 && (ysize64 % kBlockDim) == 0;
  if (div8_) {
    ysize_div8_minus_1_ = ysize32 / 8 - 1;
  } else {
    ysize_minus_1_ = ysize32 - 1;
  }

  ratio_ = FindAspectRatio(xsize32, ysize32);
  if (ratio_ == 0) {
    if (div8_) {
      xsize_div8_minus_1_ = xsize32 / 8 - 1;
    } else {
      xsize_minus_1_ = xsize32 - 1;
    }
  }
  JXL_ASSERT(xsize() == xsize64);
  JXL_ASSERT(ysize() == ysize64);
  return true;
}

size_t PreviewHeader::xsize() const {
  if (ratio_ != 0) {
    return FixedAspectRatios(ratio_).MulTruncate(
        static_cast<uint32_t>(ysize()));
  }
  return div8_ ? ((xsize_div8_minus_1_ + 1) * 8) : xsize_minus_1_ + 1;
}

SizeHeader::SizeHeader() { Bundle::Init(this); }
PreviewHeader::PreviewHeader() { Bundle::Init(this); }
AnimationHeader::AnimationHeader() { Bundle::Init(this); }

Status ReadSizeHeader(BitReader* JXL_RESTRICT reader,
                      SizeHeader* JXL_RESTRICT size) {
  return Bundle::Read(reader, size);
}

Status ReadPreviewHeader(BitReader* JXL_RESTRICT reader,
                         PreviewHeader* JXL_RESTRICT preview) {
  return Bundle::Read(reader, preview);
}

Status ReadAnimationHeader(BitReader* JXL_RESTRICT reader,
                           AnimationHeader* JXL_RESTRICT animation) {
  return Bundle::Read(reader, animation);
}

Status WriteSizeHeader(const SizeHeader& size, BitWriter* JXL_RESTRICT writer,
                       size_t layer, AuxOut* aux_out) {
  const size_t max_bits = Bundle::MaxBits(size);
  if (max_bits != SizeHeader::kMaxBits) {
    JXL_ABORT("Please update SizeHeader::kMaxBits from %zu to %zu\n",
              SizeHeader::kMaxBits, max_bits);
  }

  // Only check the number of non-extension bits (extensions are unbounded).
  // (Bundle::Write will call CanEncode again, but it is fast because SizeHeader
  // is tiny.)
  size_t extension_bits, total_bits;
  JXL_RETURN_IF_ERROR(Bundle::CanEncode(size, &extension_bits, &total_bits));
  JXL_ASSERT(total_bits - extension_bits < SizeHeader::kMaxBits);

  return Bundle::Write(size, writer, layer, aux_out);
}

Status WritePreviewHeader(const PreviewHeader& preview,
                          BitWriter* JXL_RESTRICT writer, size_t layer,
                          AuxOut* aux_out) {
  return Bundle::Write(preview, writer, layer, aux_out);
}

Status WriteAnimationHeader(const AnimationHeader& animation,
                            BitWriter* JXL_RESTRICT writer, size_t layer,
                            AuxOut* aux_out) {
  return Bundle::Write(animation, writer, layer, aux_out);
}

}  // namespace jxl
