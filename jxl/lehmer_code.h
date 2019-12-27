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

#ifndef JXL_LEHMER_CODE_H_
#define JXL_LEHMER_CODE_H_

#include <stddef.h>
#include <stdint.h>

#include "jxl/base/bits.h"
#include "jxl/base/compiler_specific.h"
#include "jxl/base/status.h"

namespace jxl {

// Permutation <=> factorial base representation (Lehmer code).

using LehmerT = uint32_t;

template <typename T>
constexpr T ValueOfLowest1Bit(T t) {
  return t & -t;
}

// Computes the Lehmer (factorial basis) code of permutation, an array of n
// unique indices in [0..n), and stores it in code[0..len). N*logN time.
// temp must have n + 1 elements but need not be initialized.
template <typename PermutationT>
void ComputeLehmerCode(const PermutationT* JXL_RESTRICT permutation,
                       uint32_t* JXL_RESTRICT temp, const size_t n,
                       LehmerT* JXL_RESTRICT code) {
  for (size_t idx = 0; idx < n + 1; ++idx) temp[idx] = 0;

  for (size_t idx = 0; idx < n; ++idx) {
    const PermutationT s = permutation[idx];

    // Compute sum in Fenwick tree
    uint32_t penalty = 0;
    uint32_t i = s + 1;
    while (i != 0) {
      penalty += temp[i];
      i &= i - 1;  // clear lowest bit
    }
    JXL_DASSERT(s >= penalty);
    code[idx] = s - penalty;
    i = s + 1;
    // Add operation in Fenwick tree
    while (i < n + 1) {
      temp[i] += 1;
      i += ValueOfLowest1Bit(i);
    }
  }
}

// Decodes the Lehmer code in code[0..n) into permutation[0..n).
// temp must have 1 << CeilLog2(n) elements but need not be initialized.
template <typename PermutationT>
void DecodeLehmerCode(const LehmerT* JXL_RESTRICT code,
                      uint32_t* JXL_RESTRICT temp, size_t n,
                      PermutationT* JXL_RESTRICT permutation) {
  JXL_DASSERT(n != 0);
  const size_t log2n = CeilLog2Nonzero(n);
  const size_t padded_n = 1ull << log2n;

  for (size_t i = 0; i < padded_n; i++) {
    const int32_t i1 = static_cast<int32_t>(i + 1);
    temp[i] = static_cast<uint32_t>(ValueOfLowest1Bit(i1));
  }

  for (size_t i = 0; i < n; i++) {
    JXL_DASSERT(code[i] + i < n);
    uint32_t rank = code[i] + 1;

    // Extract i-th unused element via implicit order-statistics tree.
    size_t bit = padded_n;
    size_t next = 0;
    for (size_t i = 0; i <= log2n; i++) {
      const size_t cand = next + bit;
      JXL_DASSERT(cand >= 1);
      bit >>= 1;
      if (temp[cand - 1] < rank) {
        next = cand;
        rank -= temp[cand - 1];
      }
    }

    permutation[i] = next;

    // Mark as used
    next += 1;
    while (next <= padded_n) {
      temp[next - 1] -= 1;
      next += ValueOfLowest1Bit(next);
    }
  }
}

}  // namespace jxl

#endif  // JXL_LEHMER_CODE_H_
