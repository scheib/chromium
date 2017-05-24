// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/testing/HistogramTester.h"

#include "base/test/histogram_tester.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

HistogramTester::HistogramTester()
    : histogram_tester_(WTF::WrapUnique(new base::HistogramTester)) {}

HistogramTester::~HistogramTester() {}

void HistogramTester::ExpectUniqueSample(
    const std::string& name,
    base::HistogramBase::Sample sample,
    base::HistogramBase::Count count) const {
  histogram_tester_->ExpectUniqueSample(name, sample, count);
}

void HistogramTester::ExpectBucketCount(
    const std::string& name,
    base::HistogramBase::Sample sample,
    base::HistogramBase::Count count) const {
  histogram_tester_->ExpectBucketCount(name, sample, count);
}

void HistogramTester::ExpectTotalCount(const std::string& name,
                                       base::HistogramBase::Count count) const {
  histogram_tester_->ExpectTotalCount(name, count);
}

base::HistogramBase::Count HistogramTester::GetBucketCount(
    const std::string& name,
    base::HistogramBase::Sample sample) const {
  return histogram_tester_->GetBucketCount(name, sample);
}

}  // namespace blink
