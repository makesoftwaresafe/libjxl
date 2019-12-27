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

#include "jpegxl/thread_parallel_runner.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>  // max
#include <atomic>
#include <condition_variable>  //NOLINT
#include <mutex>               //NOLINT
#include <thread>              //NOLINT
#include <vector>

#include "jxl/base/bits.h"
#include "jxl/base/profiler.h"
#include "jxl/base/status.h"

namespace jpegxl {

// static
JpegxlParallelRetCode ThreadParallelRunner::Runner(
    void* runner_opaque, void* jpegxl_opaque, JpegxlParallelRunInit init,
    JpegxlParallelRunFunction func, uint32_t start_range, uint32_t end_range) {
  ThreadParallelRunner* self =
      static_cast<ThreadParallelRunner*>(runner_opaque);
  if (start_range > end_range) return -1;
  if (start_range == end_range) return 0;

  int ret = init(jpegxl_opaque, std::max<size_t>(self->num_worker_threads_, 1));
  if (ret != 0) return ret;

  // Use a sequential run when num_worker_threads_ is zero since we have no
  // worker threads.
  if (self->num_worker_threads_ == 0) {
    const size_t thread = 0;
    for (uint32_t task = start_range; task < end_range; ++task) {
      func(jpegxl_opaque, task, thread);
    }
    return 0;
  }

  if (self->depth_.fetch_add(1, std::memory_order_acq_rel) != 0) {
    return -1;  // Must not re-enter.
  }

  const WorkerCommand worker_command =
      (static_cast<WorkerCommand>(start_range) << 32) + end_range;
  // Ensure the inputs do not result in a reserved command.
  JXL_ASSERT(worker_command != kWorkerWait);
  JXL_ASSERT(worker_command != kWorkerOnce);
  JXL_ASSERT(worker_command != kWorkerExit);

  self->data_func_ = func;
  self->jpegxl_opaque_ = jpegxl_opaque;
  self->num_reserved_.store(0, std::memory_order_relaxed);

  self->StartWorkers(worker_command);
  self->WorkersReadyBarrier();

  if (self->depth_.fetch_add(-1, std::memory_order_acq_rel) != 1) {
    return -1;
  }
  return 0;
}

// static
void ThreadParallelRunner::RunRange(ThreadParallelRunner* self,
                                    const WorkerCommand command,
                                    const int thread) {
  const uint32_t begin = command >> 32;
  const uint32_t end = command & 0xFFFFFFFF;
  const uint32_t num_tasks = end - begin;
  const uint32_t num_worker_threads = self->num_worker_threads_;

  // OpenMP introduced several "schedule" strategies:
  // "single" (static assignment of exactly one chunk per thread): slower.
  // "dynamic" (allocates k tasks at a time): competitive for well-chosen k.
  // "guided" (allocates k tasks, decreases k): computing k = remaining/n
  //   is faster than halving k each iteration. We prefer this strategy
  //   because it avoids user-specified parameters.

  for (;;) {
#if 0
      // dynamic
      const uint32_t my_size = std::max(num_tasks / (num_worker_threads * 4), 1);
#else
    // guided
    const uint32_t num_reserved =
        self->num_reserved_.load(std::memory_order_relaxed);
    const uint32_t num_remaining = num_tasks - num_reserved;
    const uint32_t my_size =
        std::max(num_remaining / (num_worker_threads * 4), 1u);
#endif
    const uint32_t my_begin = begin + self->num_reserved_.fetch_add(
                                          my_size, std::memory_order_relaxed);
    const uint32_t my_end = std::min(my_begin + my_size, begin + num_tasks);
    // Another thread already reserved the last task.
    if (my_begin >= my_end) {
      break;
    }
    for (uint32_t task = my_begin; task < my_end; ++task) {
      self->data_func_(self->jpegxl_opaque_, task, thread);
    }
  }
}

// static
void ThreadParallelRunner::ThreadFunc(ThreadParallelRunner* self,
                                      const int thread) {
  // Until kWorkerExit command received:
  for (;;) {
    std::unique_lock<std::mutex> lock(self->mutex_);
    // Notify main thread that this thread is ready.
    if (++self->workers_ready_ == self->num_threads_) {
      self->workers_ready_cv_.notify_one();
    }
  RESUME_WAIT:
    // Wait for a command.
    self->worker_start_cv_.wait(lock);
    const WorkerCommand command = self->worker_start_command_;
    switch (command) {
      case kWorkerWait:    // spurious wakeup:
        goto RESUME_WAIT;  // lock still held, avoid incrementing ready.
      case kWorkerOnce:
        lock.unlock();
        self->data_func_(self->jpegxl_opaque_, thread, thread);
        break;
      case kWorkerExit:
        return;  // exits thread
      default:
        lock.unlock();
        RunRange(self, command, thread);
        break;
    }
  }
}

ThreadParallelRunner::ThreadParallelRunner(const int num_worker_threads)
    : num_worker_threads_(num_worker_threads),
      num_threads_(std::max(num_worker_threads, 1)) {
  PROFILER_ZONE("ThreadParallelRunner ctor");

  JXL_CHECK(num_worker_threads >= 0);
  threads_.reserve(num_worker_threads);

  // Suppress "unused-private-field" warning.
  (void)padding1;
  (void)padding2;

  // Safely handle spurious worker wakeups.
  worker_start_command_ = kWorkerWait;

  for (int i = 0; i < num_worker_threads; ++i) {
    threads_.emplace_back(ThreadFunc, this, i);
  }

  if (num_worker_threads_ != 0) {
    WorkersReadyBarrier();
  }

  // Warm up profiler on worker threads so its expensive initialization
  // doesn't count towards other timer measurements.
  RunOnEachThread(
      [](const int task, const int thread) { PROFILER_ZONE("@InitWorkers"); });
}

ThreadParallelRunner::~ThreadParallelRunner() {
  if (num_worker_threads_ != 0) {
    StartWorkers(kWorkerExit);
  }

  for (std::thread& thread : threads_) {
    JXL_ASSERT(thread.joinable());
    thread.join();
  }
}

}  // namespace jpegxl
