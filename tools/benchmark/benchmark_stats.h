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

#ifndef TOOLS_BENCHMARK_BENCHMARK_STATS_H_
#define TOOLS_BENCHMARK_BENCHMARK_STATS_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "jxl/aux_out.h"

namespace jxl {

std::string StringPrintf(const char* format, ...);

struct JxlStats {
  JxlStats() {
    num_inputs = 0;
    aux_out = AuxOut();
  }
  void Assimilate(const JxlStats& victim) {
    num_inputs += victim.num_inputs;
    aux_out.Assimilate(victim.aux_out);
  }
  void Print() const { aux_out.Print(num_inputs); }

  size_t num_inputs;
  AuxOut aux_out;
};

// The value of an entry in the table. Depending on the ColumnType, the string,
// size_t or double should be used.
struct ColumnValue {
  std::string s;  // for TYPE_STRING
  size_t i;  // for TYPE_SIZE and TYPE_COUNT
  double f;  // for TYPE_POSITIVE_FLOAT
};

struct BenchmarkStats {
  void Assimilate(const BenchmarkStats& victim);

  std::vector<ColumnValue> ComputeColumns(const std::string& codec_desc,
                                          size_t corpus_size,
                                          size_t num_threads) const;

  std::string PrintLine(const std::string& codec_desc, size_t corpus_size,
                        size_t num_threads) const;

  void PrintMoreStats() const;

  size_t total_input_files = 0;
  size_t total_input_pixels = 0;
  size_t total_compressed_size = 0;
  size_t total_adj_compressed_size = 0;
  double total_time_encode = 0.0;
  double total_time_decode = 0.0;
  float max_distance = -1.0;  // Max butteraugli score
  // sum of 8th powers of butteraugli distmap pixels.
  double distance_p_norm = 0.0;
  // sum of 2nd powers of differences between R, G, B.
  double distance_2 = 0.0;
  std::vector<float> distances;
  size_t total_errors = 0;
  JxlStats jxl_stats;
};

std::string PrintHeader();

// Given the rows of all printed statistics, print an aggregate row.
std::string PrintAggregate(
    const std::vector<std::vector<ColumnValue>>& aggregate);

}  // namespace jxl

#endif  // TOOLS_BENCHMARK_BENCHMARK_STATS_H_
