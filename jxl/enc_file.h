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

#ifndef JXL_ENC_FILE_H_
#define JXL_ENC_FILE_H_

// Facade for JXL encoding.

#include "jxl/aux_out.h"
#include "jxl/aux_out_fwd.h"
#include "jxl/base/data_parallel.h"
#include "jxl/base/padded_bytes.h"
#include "jxl/base/status.h"
#include "jxl/codec_in_out.h"
#include "jxl/enc_cache.h"
#include "jxl/enc_params.h"

namespace jxl {

// Compresses pixels from `io` (given in any ColorEncoding).
// `io->metadata.original` must be set.
Status EncodeFile(const CompressParams& params, const CodecInOut* io,
                  PassesEncoderState* passes_enc_state, PaddedBytes* compressed,
                  AuxOut* aux_out = nullptr, ThreadPool* pool = nullptr);

// Backwards-compatible interface. Don't use in new code.
// TODO(deymo): Remove this function once we migrate users to C encoder API.
struct FrameEncCache {};
JXL_INLINE Status EncodeFile(const CompressParams& params, const CodecInOut* io,
                             FrameEncCache* /* unused */,
                             PaddedBytes* compressed, AuxOut* aux_out = nullptr,
                             ThreadPool* pool = nullptr) {
  PassesEncoderState passes_enc_state;
  return EncodeFile(params, io, &passes_enc_state, compressed, aux_out, pool);
}

}  // namespace jxl

#endif  // JXL_ENC_FILE_H_
