// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/web_state/ui/crw_web_view_scroll_view_proxy.h"

#import <objc/runtime.h>

#include "base/auto_reset.h"
#import "base/ios/crb_protocol_observers.h"
#include "base/mac/foundation_util.h"
#import "base/mac/scoped_nsobject.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface CRWWebViewScrollViewProxy () {
  __weak UIScrollView* _scrollView;
  base::scoped_nsobject<id> _observers;
}

// Returns the key paths that need to be observed for UIScrollView.
+ (NSArray*)scrollViewObserverKeyPaths;

// Adds and removes |self| as an observer for |scrollView| with key paths
// returned by |+scrollViewObserverKeyPaths|.
- (void)startObservingScrollView:(UIScrollView*)scrollView;
- (void)stopObservingScrollView:(UIScrollView*)scrollView;

@end

@implementation CRWWebViewScrollViewProxy

- (instancetype)init {
  self = [super init];
  if (self) {
    Protocol* protocol = @protocol(CRWWebViewScrollViewProxyObserver);
    _observers.reset([CRBProtocolObservers observersWithProtocol:protocol]);
  }
  return self;
}

- (void)dealloc {
  [self stopObservingScrollView:_scrollView];
}

- (void)addGestureRecognizer:(UIGestureRecognizer*)gestureRecognizer {
  [_scrollView addGestureRecognizer:gestureRecognizer];
}

- (void)removeGestureRecognizer:(UIGestureRecognizer*)gestureRecognizer {
  [_scrollView removeGestureRecognizer:gestureRecognizer];
}

- (void)addObserver:(id<CRWWebViewScrollViewProxyObserver>)observer {
  [_observers addObserver:observer];
}

- (void)removeObserver:(id<CRWWebViewScrollViewProxyObserver>)observer {
  [_observers removeObserver:observer];
}

- (void)setScrollView:(UIScrollView*)scrollView {
  if (_scrollView == scrollView)
    return;
  [_scrollView setDelegate:nil];
  [self stopObservingScrollView:_scrollView];
  DCHECK(!scrollView.delegate);
  scrollView.delegate = self;
  [self startObservingScrollView:scrollView];
  _scrollView = scrollView;
  [_observers webViewScrollViewProxyDidSetScrollView:self];
}

- (CGRect)frame {
  return _scrollView ? [_scrollView frame] : CGRectZero;
}

- (BOOL)isScrollEnabled {
  return [_scrollView isScrollEnabled];
}

- (void)setScrollEnabled:(BOOL)scrollEnabled {
  [_scrollView setScrollEnabled:scrollEnabled];
}

- (BOOL)bounces {
  return [_scrollView bounces];
}

- (void)setBounces:(BOOL)bounces {
  [_scrollView setBounces:bounces];
}

- (BOOL)isDecelerating {
  return [_scrollView isDecelerating];
}

- (BOOL)isDragging {
  return [_scrollView isDragging];
}

- (BOOL)isZooming {
  return [_scrollView isZooming];
}

- (CGFloat)zoomScale {
  return [_scrollView zoomScale];
}

- (void)setContentOffset:(CGPoint)contentOffset {
  [_scrollView setContentOffset:contentOffset];
}

- (CGPoint)contentOffset {
  return _scrollView ? [_scrollView contentOffset] : CGPointZero;
}

- (void)setContentInset:(UIEdgeInsets)contentInset {
  [_scrollView setContentInset:contentInset];
}

- (UIEdgeInsets)contentInset {
  return _scrollView ? [_scrollView contentInset] : UIEdgeInsetsZero;
}

- (void)setScrollIndicatorInsets:(UIEdgeInsets)scrollIndicatorInsets {
  [_scrollView setScrollIndicatorInsets:scrollIndicatorInsets];
}

- (UIEdgeInsets)scrollIndicatorInsets {
  return _scrollView ? [_scrollView scrollIndicatorInsets] : UIEdgeInsetsZero;
}

- (void)setContentSize:(CGSize)contentSize {
  [_scrollView setContentSize:contentSize];
}

- (CGSize)contentSize {
  return _scrollView ? [_scrollView contentSize] : CGSizeZero;
}

- (void)setContentOffset:(CGPoint)contentOffset animated:(BOOL)animated {
  [_scrollView setContentOffset:contentOffset animated:animated];
}

- (BOOL)scrollsToTop {
  return [_scrollView scrollsToTop];
}

- (void)setScrollsToTop:(BOOL)scrollsToTop {
  [_scrollView setScrollsToTop:scrollsToTop];
}

- (UIPanGestureRecognizer*)panGestureRecognizer {
  return [_scrollView panGestureRecognizer];
}

- (NSArray*)gestureRecognizers {
  return [_scrollView gestureRecognizers];
}

#pragma mark -
#pragma mark UIScrollViewDelegate callbacks

- (void)scrollViewDidScroll:(UIScrollView*)scrollView {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewDidScroll:self];
}

- (void)scrollViewWillBeginDragging:(UIScrollView*)scrollView {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewWillBeginDragging:self];

  // TODO(crbug.com/555723) Remove this once the fix to
  // https://bugs.webkit.org/show_bug.cgi?id=148086 makes it's way in to iOS.
  scrollView.decelerationRate = UIScrollViewDecelerationRateNormal;
}

- (void)scrollViewWillEndDragging:(UIScrollView*)scrollView
                     withVelocity:(CGPoint)velocity
              targetContentOffset:(inout CGPoint*)targetContentOffset {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewWillEndDragging:self
                                  withVelocity:velocity
                           targetContentOffset:targetContentOffset];
}

- (void)scrollViewDidEndDragging:(UIScrollView*)scrollView
                  willDecelerate:(BOOL)decelerate {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewDidEndDragging:self willDecelerate:decelerate];
}

- (void)scrollViewDidEndDecelerating:(UIScrollView*)scrollView {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewDidEndDecelerating:self];
}

- (void)scrollViewDidEndScrollingAnimation:(UIScrollView*)scrollView {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewDidEndScrollingAnimation:self];
}

- (BOOL)scrollViewShouldScrollToTop:(UIScrollView*)scrollView {
  DCHECK_EQ(_scrollView, scrollView);
  __block BOOL shouldScrollToTop = YES;
  [_observers executeOnObservers:^(id observer) {
    if ([observer respondsToSelector:@selector
                  (webViewScrollViewShouldScrollToTop:)]) {
      shouldScrollToTop = shouldScrollToTop &&
                          [observer webViewScrollViewShouldScrollToTop:self];
    }
  }];
  return shouldScrollToTop;
}

- (void)scrollViewDidZoom:(UIScrollView*)scrollView {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewDidZoom:self];
}

- (void)scrollViewWillBeginZooming:(UIScrollView*)scrollView
                          withView:(UIView*)view {
  DCHECK_EQ(_scrollView, scrollView);
  [_observers webViewScrollViewWillBeginZooming:self];
}

#pragma mark -

+ (NSArray*)scrollViewObserverKeyPaths {
  return @[ @"contentSize" ];
}

- (void)startObservingScrollView:(UIScrollView*)scrollView {
  for (NSString* keyPath in [[self class] scrollViewObserverKeyPaths])
    [scrollView addObserver:self forKeyPath:keyPath options:0 context:nil];
}

- (void)stopObservingScrollView:(UIScrollView*)scrollView {
  for (NSString* keyPath in [[self class] scrollViewObserverKeyPaths])
    [scrollView removeObserver:self forKeyPath:keyPath];
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
  DCHECK_EQ(object, _scrollView);
  if ([keyPath isEqualToString:@"contentSize"])
    [_observers webViewScrollViewDidResetContentSize:self];
}

@end
