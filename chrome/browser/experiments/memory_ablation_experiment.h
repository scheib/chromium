// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXPERIMENTS_MEMORY_ABLATION_EXPERIMENT_H_
#define CHROME_BROWSER_EXPERIMENTS_MEMORY_ABLATION_EXPERIMENT_H_

#include <memory>

#include "base/feature_list.h"
#include "base/memory/ref_counted.h"

namespace base {
class SequencedTaskRunner;
}

extern const base::Feature kMemoryAblationFeature;
extern const char kMemoryAblationFeatureSizeParam[];
extern const char kMemoryAblationFeatureMinRAMParam[];
extern const char kMemoryAblationFeatureMaxRAMParam[];

/* When enabled, this experiment allocates a chunk of memory to study
 * correlation between memory usage and performance metrics.
 */
class MemoryAblationExperiment {
 public:
  ~MemoryAblationExperiment();

  // Starts the experiment if corresponding field trial is enabled
  static void MaybeStart(scoped_refptr<base::SequencedTaskRunner> task_runner);

 private:
  MemoryAblationExperiment();

  void Start(scoped_refptr<base::SequencedTaskRunner> task_runner, size_t size);

  void AllocateMemory(size_t size);
  void TouchMemory(size_t offset);
  void ScheduleTouchMemory(size_t offset);

  static MemoryAblationExperiment* GetInstance();

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  size_t memory_size_ = 0;
  std::unique_ptr<uint8_t[]> memory_;
};

#endif  // CHROME_BROWSER_EXPERIMENTS_MEMORY_ABLATION_EXPERIMENT_H_
