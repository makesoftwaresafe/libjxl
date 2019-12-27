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

#ifndef JXL_DEC_FILE_H_
#define JXL_DEC_FILE_H_

// Top-level interface for JXL decoding.

#include <stdint.h>

#include "jxl/aux_out.h"
#include "jxl/aux_out_fwd.h"
#include "jxl/base/data_parallel.h"
#include "jxl/base/padded_bytes.h"
#include "jxl/base/span.h"
#include "jxl/base/status.h"
#include "jxl/codec_in_out.h"
#include "jxl/dec_params.h"

namespace jxl {

// Implementation detail: currently decodes to linear sRGB. The contract is:
// `io` appears 'identical' (modulo compression artifacts) to the encoder input
// in a color-aware viewer. Note that `io->metadata.color_encoding`
// identifies the color space that was passed to the encoder; clients that want
// that same encoding must call `io->TransformTo` afterwards.
Status DecodeFile(const DecompressParams& params,
                  const Span<const uint8_t> file, CodecInOut* io,
                  AuxOut* aux_out = nullptr, ThreadPool* pool = nullptr);

static inline Status DecodeFile(const DecompressParams& params,
                                const PaddedBytes& file, CodecInOut* io,
                                AuxOut* aux_out = nullptr,
                                ThreadPool* pool = nullptr) {
  return DecodeFile(params, Span<const uint8_t>(file), io, aux_out, pool);
}

}  // namespace jxl

#endif  // JXL_DEC_FILE_H_
