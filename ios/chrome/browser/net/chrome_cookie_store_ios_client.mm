// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/net/chrome_cookie_store_ios_client.h"

#include "base/logging.h"
#include "base/task_scheduler/post_task.h"
#import "ios/chrome/browser/browsing_data/browsing_data_change_listening.h"
#include "ios/web/public/web_thread.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

ChromeCookieStoreIOSClient::ChromeCookieStoreIOSClient(
    id<BrowsingDataChangeListening> browsing_data_change_listener)
    : browsing_data_change_listener_(browsing_data_change_listener) {
  DCHECK(browsing_data_change_listener);
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
}

void ChromeCookieStoreIOSClient::DidChangeCookieStorage() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  [browsing_data_change_listener_ didChangeCookieStorage];
}

scoped_refptr<base::SequencedTaskRunner>
ChromeCookieStoreIOSClient::GetTaskRunner() const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return base::CreateSequencedTaskRunnerWithTraits(
      {base::MayBlock(), base::TaskPriority::BACKGROUND});
}
