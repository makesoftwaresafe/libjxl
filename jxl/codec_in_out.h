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

#ifndef JXL_CODEC_IN_OUT_H_
#define JXL_CODEC_IN_OUT_H_

// Holds inputs/outputs for decoding/encoding images.

#include <stddef.h>

#include <utility>
#include <vector>

#include "jxl/base/data_parallel.h"
#include "jxl/common.h"
#include "jxl/frame_header.h"
#include "jxl/headers.h"
#include "jxl/image.h"
#include "jxl/image_bundle.h"

namespace jxl {

// Per-channel interval, used to convert between (full-range) external and
// (bounded or unbounded) temp values. See external_image.cc for the definitions
// of temp/external.
struct CodecInterval {
  CodecInterval() = default;
  constexpr CodecInterval(float min, float max) : min(min), width(max - min) {}
  // Defaults for temp.
  float min = 0.0f;
  float width = 1.0f;
};

using CodecIntervals = std::array<CodecInterval, 4>;  // RGB[A] or Y[A]

// Allows passing arbitrary metadata to decoders (required for PNM).
class DecoderHints {
 public:
  // key=color_space, value=Description(c/pp): specify the ColorEncoding of
  //   the pixels for decoding. Otherwise, if the codec did not obtain an ICC
  //   profile from the image, assume sRGB.
  //
  // Strings are taken from the command line, so avoid spaces for convenience.
  void Add(const std::string& key, const std::string& value) {
    kv_.emplace_back(key, value);
  }

  // Calls func(key, value) in order of Add.
  template <class Func>
  void Foreach(const Func& func) const {
    for (const KeyValue& kv : kv_) {
      func(kv.key, kv.value);
    }
  }

 private:
  // Splitting into key/value avoids parsing in each codec.
  struct KeyValue {
    KeyValue(std::string key, std::string value)
        : key(std::move(key)), value(std::move(value)) {}

    std::string key;
    std::string value;
  };

  std::vector<KeyValue> kv_;
};

// Optional text/EXIF metadata.
struct Blobs {
  PaddedBytes exif;
  PaddedBytes iptc;
  PaddedBytes jumbf;
  PaddedBytes xmp;
};

// Holds a preview, a main image or one or more frames, plus the inputs/outputs
// to/from decoding/encoding.
class CodecInOut {
 public:
  CodecInOut() : preview_frame(&metadata) {
    frames.reserve(1);
    frames.emplace_back(&metadata);
  }

  // Move-only.
  CodecInOut(CodecInOut&&) = default;
  CodecInOut& operator=(CodecInOut&&) = default;

  ImageBundle& Main() {
    JXL_DASSERT(frames.size() == 1);
    return frames[0];
  }
  const ImageBundle& Main() const {
    JXL_DASSERT(frames.size() == 1);
    return frames[0];
  }

  // If c_current.IsGray(), all planes must be identical.
  void SetFromImage(Image3F&& color, const ColorEncoding& c_current) {
    Main().SetFromImage(std::move(color), c_current);
  }

  void CheckMetadata() const {
    JXL_CHECK(metadata.bits_per_sample != 0);
    JXL_CHECK(!metadata.color_encoding.icc.empty());

    if (preview_frame.xsize() != 0) preview_frame.VerifyMetadata();
    JXL_CHECK(preview_frame.metadata() == &metadata);

    for (const ImageBundle& ib : frames) {
      ib.VerifyMetadata();
      JXL_CHECK(ib.metadata() == &metadata);
    }
  }

  size_t xsize() const { return Main().xsize(); }
  size_t ysize() const { return Main().ysize(); }
  void ShrinkTo(size_t xsize, size_t ysize) {
    // preview is unaffected.
    for (ImageBundle& ib : frames) {
      ib.ShrinkTo(xsize, ysize);
    }
  }

  template <typename T>
  Status VerifyDimensions(T xs, T ys) const {
    if (xs == 0 || ys == 0) return JXL_FAILURE("Empty image.");
    if (xs > dec_max_xsize) return JXL_FAILURE("Image too wide.");
    if (ys > dec_max_ysize) return JXL_FAILURE("Image too tall.");

    const uint64_t num_pixels = uint64_t(xs) * ys;
    if (num_pixels > dec_max_pixels) return JXL_FAILURE("Image too big.");

    return true;
  }

  // Calls TransformTo for each ImageBundle (preview/frames).
  Status TransformTo(const ColorEncoding& c_desired,
                     ThreadPool* pool = nullptr) {
    if (metadata.m2.have_preview) {
      JXL_RETURN_IF_ERROR(preview_frame.TransformTo(c_desired, pool));
    }
    for (ImageBundle& ib : frames) {
      JXL_RETURN_IF_ERROR(ib.TransformTo(c_desired, pool));
    }
    return true;
  }

  // -- ENCODER OUTPUT:

  // Size [bytes] of encoded bitstream after encoding / before decoding.
  mutable size_t enc_size;

  // Encoder-specific function of its bits_per_sample argument. Used to compute
  // error tolerance in round trips.
  mutable size_t enc_bits_per_sample;

  // -- DECODER INPUT:

  // Upper limit on pixel dimensions/area, enforced by DimensionsWithinLimits
  // (called from decoders). Fuzzers set smaller values to limit memory use.
  uint32_t dec_max_xsize = 0xFFFFFFFFu;
  uint32_t dec_max_ysize = 0xFFFFFFFFu;
  uint64_t dec_max_pixels = ~0ull;

  // Used to set c_current for codecs that lack color space metadata.
  DecoderHints dec_hints;

  // -- DECODER OUTPUT

  // Total number of pixels decoded (may differ from #frames * xsize * ysize
  // if frames are cropped)
  uint64_t dec_pixels = 0;

  // -- DECODER OUTPUT, ENCODER INPUT:

  // Metadata stored into / retrieved from bitstreams.

  Blobs blobs;
  ImageMetadata metadata;  // applies to preview and all frames

  // If metadata.have_preview:
  PreviewHeader preview;
  ImageBundle preview_frame;

  // If metadata.have_animation:
  AnimationHeader animation;
  std::vector<AnimationFrame> animation_frames;

  std::vector<ImageBundle> frames;  // size=1 if !metadata.have_animation

  bool use_sjpeg = false;
  // If the image should be written to a JPEG, use this quality for encoding.
  // If the image was read from a JPEG, this represents the
  size_t jpeg_quality;
};

}  // namespace jxl

#endif  // JXL_CODEC_IN_OUT_H_
