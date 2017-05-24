// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/callback_counter.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

CallbackCounter::CallbackCounter(const FinalCallback& final_callback)
    : callback_count_(0U), final_callback_(final_callback) {
  DCHECK(!final_callback.is_null());
}

CallbackCounter::~CallbackCounter() {
  DCHECK_EQ(0U, callback_count_);
}

void CallbackCounter::IncrementCount() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!final_callback_.is_null());
  ++callback_count_;
}

void CallbackCounter::DecrementCount() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(callback_count_);

  --callback_count_;
  if (callback_count_ == 0) {
    final_callback_.Run();
    final_callback_.Reset();
  }
}
