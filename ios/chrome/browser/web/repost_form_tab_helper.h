// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_WEB_REPOST_FORM_TAB_HELPER_H_
#define IOS_CHROME_BROWSER_WEB_REPOST_FORM_TAB_HELPER_H_

#include <CoreGraphics/CoreGraphics.h>

#include "base/callback.h"
#include "base/macros.h"
#import "base/mac/scoped_nsobject.h"
#include "ios/web/public/web_state/web_state_observer.h"
#import "ios/web/public/web_state/web_state_user_data.h"

@class RepostFormCoordinator;

// Allows presenting a repost form dialog. Listens to web::WebState activity
// and dismisses the dialog when necessary.
class RepostFormTabHelper : public web::WebStateUserData<RepostFormTabHelper>,
                            public web::WebStateObserver {
 public:
  explicit RepostFormTabHelper(web::WebState* web_state);
  ~RepostFormTabHelper() override;

  // Presents a repost form dialog at the given |location|. |callback| is called
  // with true if the repost was confirmed and with false if it was cancelled.
  void PresentDialog(CGPoint location,
                     const base::Callback<void(bool)>& callback);

 private:
  // web::WebStateObserver overrides:
  void DidStartNavigation(web::NavigationContext* navigation_context) override;
  void WebStateDestroyed() override;

  // Coordinates repost form dialog presentation.
  base::scoped_nsobject<RepostFormCoordinator> coordinator_;

  DISALLOW_COPY_AND_ASSIGN(RepostFormTabHelper);
};

#endif  // IOS_CHROME_BROWSER_WEB_REPOST_FORM_TAB_HELPER_H_
