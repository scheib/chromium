// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_VIEW_PUBLIC_CWV_SCROLL_VIEW_H_
#define IOS_WEB_VIEW_PUBLIC_CWV_SCROLL_VIEW_H_

#import <ChromeWebView/cwv_export.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

@protocol CWVScrollViewDelegate;

// Scroll view inside the web view. This is not a subclass of UIScrollView
// because the underlying //ios/web API only exposes a proxy object of the
// scroll view, not the raw UIScrollView.
//
// These methods are forwarded to the internal UIScrollView. Please see the
// <UIKit/UIScrollView.h> documentation for details about the following methods.
//
// TODO(crbug.com/719323): Add nullability annotations.
CWV_EXPORT
@interface CWVScrollView : NSObject

@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic) CGPoint contentOffset;
@property(nonatomic) UIEdgeInsets scrollIndicatorInsets;
@property(nonatomic, weak) id<CWVScrollViewDelegate> delegate;
@property(nonatomic, readonly, getter=isDecelerating) BOOL decelerating;
@property(nonatomic, readonly, getter=isDragging) BOOL dragging;
@property(nonatomic) BOOL scrollsToTop;
@property(nonatomic, readonly) UIPanGestureRecognizer* panGestureRecognizer;

// KVO compliant.
@property(nonatomic, readonly) CGSize contentSize;

// Be careful when using this property. There's a bug with the
// underlying WKWebView where the web view does not respect contentInsets
// properly when laying out content and calculating innerHeight for Javascript.
// Content is laid out based on the entire height of the web view rather than
// the height between the top and bottom insets.
// https://bugs.webkit.org/show_bug.cgi?id=134230
// rdar://23584409 (not available on Open Radar)
@property(nonatomic) UIEdgeInsets contentInset;

- (void)setContentOffset:(CGPoint)contentOffset animated:(BOOL)animated;
- (void)addGestureRecognizer:(UIGestureRecognizer*)gestureRecognizer;
- (void)removeGestureRecognizer:(UIGestureRecognizer*)gestureRecognizer;

@end

#endif  // IOS_WEB_VIEW_PUBLIC_CWV_SCROLL_VIEW_H_
