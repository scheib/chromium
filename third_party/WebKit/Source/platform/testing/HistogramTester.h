// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HistogramTester_h
#define HistogramTester_h

#include "platform/Histogram.h"
#include <memory>

namespace base {
class HistogramTester;
}

namespace blink {

// Blink interface for base::HistogramTester.
class HistogramTester {
 public:
  HistogramTester();
  ~HistogramTester();

  void ExpectUniqueSample(const std::string& name,
                          base::HistogramBase::Sample,
                          base::HistogramBase::Count) const;
  void ExpectBucketCount(const std::string& name,
                         base::HistogramBase::Sample,
                         base::HistogramBase::Count) const;
  void ExpectTotalCount(const std::string& name,
                        base::HistogramBase::Count) const;
  base::HistogramBase::Count GetBucketCount(const std::string& name,
                                            base::HistogramBase::Sample) const;

 private:
  std::unique_ptr<base::HistogramTester> histogram_tester_;
};

}  // namespace blink

#endif  // HistogramTester_h
