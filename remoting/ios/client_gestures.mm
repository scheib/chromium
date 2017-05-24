// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/ios/client_gestures.h"

#import "remoting/ios/session/remoting_client.h"

#include "base/logging.h"
#include "remoting/client/ui/gesture_interpreter.h"

remoting::GestureInterpreter::GestureState toGestureState(
    UIGestureRecognizerState state) {
  switch (state) {
    case UIGestureRecognizerStateBegan:
      return remoting::GestureInterpreter::GESTURE_BEGAN;
    case UIGestureRecognizerStateChanged:
      return remoting::GestureInterpreter::GESTURE_CHANGED;
    default:
      return remoting::GestureInterpreter::GESTURE_ENDED;
  }
}

@implementation ClientGestures

- (instancetype)initWithView:(UIView*)view client:(RemotingClient*)client {
  _view = view;
  _client = client;

  _inputScheme = HostInputSchemeTouch;

  _longPressRecognizer = [[UILongPressGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(longPressGestureTriggered:)];
  _longPressRecognizer.delegate = self;
  [view addGestureRecognizer:_longPressRecognizer];

  _panRecognizer = [[UIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(panGestureTriggered:)];
  _panRecognizer.minimumNumberOfTouches = 1;
  _panRecognizer.maximumNumberOfTouches = 2;
  _panRecognizer.delegate = self;
  [view addGestureRecognizer:_panRecognizer];

  _flingRecognizer = [[UIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(flingGestureTriggered:)];
  _flingRecognizer.minimumNumberOfTouches = 1;
  _flingRecognizer.maximumNumberOfTouches = 1;
  _flingRecognizer.delegate = self;
  [view addGestureRecognizer:_flingRecognizer];

  _scrollRecognizer = [[UIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(scrollGestureTriggered:)];
  _scrollRecognizer.minimumNumberOfTouches = 2;
  _scrollRecognizer.maximumNumberOfTouches = 2;
  _scrollRecognizer.delegate = self;
  [view addGestureRecognizer:_scrollRecognizer];

  _threeFingerPanRecognizer = [[UIPanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(threeFingerPanGestureTriggered:)];
  _threeFingerPanRecognizer.minimumNumberOfTouches = 3;
  _threeFingerPanRecognizer.maximumNumberOfTouches = 3;
  _threeFingerPanRecognizer.delegate = self;
  [view addGestureRecognizer:_threeFingerPanRecognizer];

  _pinchRecognizer = [[UIPinchGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(pinchGestureTriggered:)];
  _pinchRecognizer.delegate = self;
  [view addGestureRecognizer:_pinchRecognizer];

  _singleTapRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(tapGestureTriggered:)];
  _singleTapRecognizer.delegate = self;
  [view addGestureRecognizer:_singleTapRecognizer];

  _twoFingerTapRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(twoFingerTapGestureTriggered:)];
  _twoFingerTapRecognizer.numberOfTouchesRequired = 2;
  _twoFingerTapRecognizer.delegate = self;
  [view addGestureRecognizer:_twoFingerTapRecognizer];

  _threeFingerTapRecognizer = [[UITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(threeFingerTapGestureTriggered:)];
  _threeFingerTapRecognizer.numberOfTouchesRequired = 3;
  _threeFingerPanRecognizer.delegate = self;
  [view addGestureRecognizer:_threeFingerTapRecognizer];

  _inputScheme = HostInputSchemeTouch;

  [_singleTapRecognizer requireGestureRecognizerToFail:_twoFingerTapRecognizer];
  [_twoFingerTapRecognizer
      requireGestureRecognizerToFail:_threeFingerTapRecognizer];
  [_pinchRecognizer requireGestureRecognizerToFail:_singleTapRecognizer];
  [_panRecognizer requireGestureRecognizerToFail:_singleTapRecognizer];
  [_threeFingerPanRecognizer
      requireGestureRecognizerToFail:_threeFingerTapRecognizer];
  [_panRecognizer requireGestureRecognizerToFail:_scrollRecognizer];

  _edgeGesture = [[UIScreenEdgePanGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(edgeGestureTriggered:)];
  //_edgeGesture.edges = UIRectEdgeLeft;
  _edgeGesture.delegate = self;
  [view addGestureRecognizer:_edgeGesture];

  _swipeGesture = [[UISwipeGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(swipeGestureTriggered:)];
  _swipeGesture.numberOfTouchesRequired = 2;
  _swipeGesture.delegate = self;
  [view addGestureRecognizer:_swipeGesture];

  return self;
}

// TODO(nicholss): The following several methods have been commented out.
// Integreation with the original implementation will be done with the app
// is able to run to the point of interaction and debugging can happen.
// I would prefer to leave this here rather than deleting because it is close
// to the implementation that will be used client gestures integration.
// This is a new class that was derived from the original source which had
// the implementation mixed in with the host controller and was a huge mess.

// Resize the view of the desktop - Zoom in/out.  This can occur during a Pan.
- (IBAction)pinchGestureTriggered:(UIPinchGestureRecognizer*)sender {
  // LOG_TRACE(INFO) << "pinchGestureTriggered";
  CGPoint pivot = [sender locationInView:_view];
  _client.gestureInterpreter->Zoom(pivot.x, pivot.y, sender.scale,
                                   toGestureState([sender state]));

  sender.scale = 1.0;  // reset scale so next iteration is a relative ratio
}

- (IBAction)tapGestureTriggered:(UITapGestureRecognizer*)sender {
  CGPoint touchPoint = [sender locationInView:_view];
  _client.gestureInterpreter->Tap(touchPoint.x, touchPoint.y);
}

// Change position of scene.  This can occur during a pinch or long press.
// Or perform a Mouse Wheel Scroll.
- (IBAction)panGestureTriggered:(UIPanGestureRecognizer*)sender {
  if ([sender state] == UIGestureRecognizerStateChanged) {
    CGPoint translation = [sender translationInView:_view];
    _client.gestureInterpreter->Pan(translation.x, translation.y);

    // Reset translation so next iteration is relative
    [sender setTranslation:CGPointZero inView:_view];
  }

  // LOG_TRACE(INFO) << "panGestureTriggered";
  //   CGPoint translation = [sender translationInView:self.view];
  //   // If we start with 2 touches, and the pinch gesture is not in progress
  //   yet,
  //   // then disable it, so mouse scrolling and zoom do not occur at the same
  //   // time.
  //   if ([sender numberOfTouches] == 2 &&
  //       [sender state] == UIGestureRecognizerStateBegan &&
  //       !(_pinchRecognizer.state == UIGestureRecognizerStateBegan ||
  //         _pinchRecognizer.state == UIGestureRecognizerStateChanged)) {
  //     _pinchRecognizer.enabled = NO;
  //   }
  //
  //   if (!_pinchRecognizer.enabled) {
  //     // Began with 2 touches, so this is a scroll event.
  //     translation.x *= kMouseWheelSensitivity;
  //     translation.y *= kMouseWheelSensitivity;
  //     // [Utility mouseScroll:_clientToHostProxy
  //     //                   at:_scene.mousePosition
  //     //                delta:webrtc::DesktopVector(translation.x,
  //     translation.y)];
  //   } else {
  //     // Did not begin with 2 touches, doing a pan event.
  //     if ([sender state] == UIGestureRecognizerStateChanged) {
  //       CGPoint translation = [sender translationInView:self.view];
  //       BOOL shouldApplyPanAndTapBounding =
  //           _inputScheme == HostInputSchemeTouch &&
  //           [_longPressRecognizer state] != UIGestureRecognizerStateChanged;
  //
  //       if (shouldApplyPanAndTapBounding) {
  //         // Reverse the orientation on both axes.
  //         translation = CGPointMake(-translation.x, -translation.y);
  //       }
  //
  //       // if (shouldApplyPanAndTapBounding) {
  //       //   // Stop the translation as soon as the view becomes anchored.
  //       //   if ([SceneView
  //       //           couldMouseMoveTowardAnchorWithTranslation:translation.x
  //       // isAnchoredLow:_scene.anchored.left
  //       // isAnchoredHigh:_scene.anchored
  //       //                                                         .right]) {
  //       //     translation = CGPointMake(0, translation.y);
  //       //   }
  //       //
  //       //   if ([SceneView
  //       //           couldMouseMoveTowardAnchorWithTranslation:translation.y
  //       // isAnchoredLow:_scene.anchored.top
  //       // isAnchoredHigh:_scene.anchored
  //       //                                                         .bottom])
  //       {
  //       //     translation = CGPointMake(translation.x, 0);
  //       //   }
  //       // }
  //
  //       [self applySceneChange:translation scaleBy:1.0];
  //
  //       if (_inputScheme == HostInputSchemeTouch &&
  //           [_longPressRecognizer state] == UIGestureRecognizerStateChanged
  //           &&
  //           [sender numberOfTouches] == 1) {
  //         // [_scene
  //         //     setMouseLocationFromLocationInView:[sender
  //         // locationInView:self.view]];
  //
  // //        [Utility moveMouse:_clientToHostProxy at:_scene.mousePosition];
  //       }
  //
  //     } else if ([sender state] == UIGestureRecognizerStateEnded) {
  //       // After user removes their fingers from the screen
  //       // apply an acceleration effect.
  //       CGPoint velocity = [sender velocityInView:self.view];
  //
  //       if (_inputScheme == HostInputSchemeTouch) {
  //         // Reverse the orientation on both axes.
  //         velocity = CGPointMake(-velocity.x, -velocity.y);
  //       }
  // //      [_scene setPanVelocity:velocity];
  //     }
  //   }
  //
  //   // Finished the event chain.
  //   if (!([sender state] == UIGestureRecognizerStateBegan ||
  //         [sender state] == UIGestureRecognizerStateChanged)) {
  //     _pinchRecognizer.enabled = YES;
  //   }
  //
  //   // Reset translation so next iteration is relative.  Wait until a changed
  //   // event in order to also capture the portion of the translation that
  //   occurred
  //   // between the Began and Changed States.
  //   if ([sender state] == UIGestureRecognizerStateChanged) {
  //     [sender setTranslation:CGPointZero inView:self.view];
  //   }
}

// Do fling on the viewport. This will happen at the end of the one-finger
// panning.
- (IBAction)flingGestureTriggered:(UIPanGestureRecognizer*)sender {
  if ([sender state] == UIGestureRecognizerStateEnded) {
    CGPoint velocity = [sender velocityInView:_view];
    if (velocity.x != 0 || velocity.y != 0) {
      _client.gestureInterpreter->OneFingerFling(velocity.x, velocity.y);
    }
  }
}

- (IBAction)scrollGestureTriggered:(UIPanGestureRecognizer*)sender {
  if ([sender state] == UIGestureRecognizerStateEnded) {
    CGPoint velocity = [sender velocityInView:_view];
    _client.gestureInterpreter->ScrollWithVelocity(velocity.x, velocity.y);
    return;
  }

  CGPoint scrollPoint = [sender locationInView:_view];
  CGPoint translation = [sender translationInView:_view];
  _client.gestureInterpreter->Scroll(scrollPoint.x, scrollPoint.y,
                                     translation.x, translation.y);

  // Reset translation so next iteration is relative
  [sender setTranslation:CGPointZero inView:_view];
}

// Click-Drag mouse operation.  This can occur during a Pan.
- (IBAction)longPressGestureTriggered:(UILongPressGestureRecognizer*)sender {
  CGPoint touchPoint = [sender locationInView:_view];
  _client.gestureInterpreter->Drag(touchPoint.x, touchPoint.y,
                                   toGestureState([sender state]));

  // LOG_TRACE(INFO) << "longPressGestureTriggered";
  // if ([sender state] == UIGestureRecognizerStateBegan) {
  //   if (_inputScheme == HostInputSchemeTouch) {
  //     CGPoint touchPoint = [sender locationInView:self.view];
  //     [_scene setMouseLocationFromLocationInView:touchPoint];
  //   }
  //   [_clientToHostProxy mouseAction:_scene.mousePosition
  //                        wheelDelta:webrtc::DesktopVector(0, 0)
  //                       whichButton:1
  //                        buttonDown:YES];
  //   if (_inputScheme == HostInputSchemeTouch) {
  //     // location is going to be under the user's finger
  //     // create a bigger bubble.
  //     _circle.expandedRadius = 110.0f;
  //   } else {
  //     _circle.expandedRadius = 11.0f;
  //   }
  //
  //   [_circle doExpandingAnimationAtLocation:[_scene mouseLocationInView]];
  // } else if (!([sender state] == UIGestureRecognizerStateBegan ||
  //              [sender state] == UIGestureRecognizerStateChanged)) {
  //   [_clientToHostProxy mouseAction:_scene.mousePosition
  //                        wheelDelta:webrtc::DesktopVector(0, 0)
  //                       whichButton:1
  //                        buttonDown:NO];
  //   if (_inputScheme == HostInputSchemeTouch) {
  //     // Return to the center.
  //     [_scene centerMouseInView];
  //   }
  // }
}

- (IBAction)twoFingerTapGestureTriggered:(UITapGestureRecognizer*)sender {
  CGPoint touchPoint = [sender locationInView:_view];
  _client.gestureInterpreter->TwoFingerTap(touchPoint.x, touchPoint.y);
}

- (IBAction)threeFingerTapGestureTriggered:(UITapGestureRecognizer*)sender {
  // LOG_TRACE(INFO) << "threeFingerTapGestureTriggered";
  if (_inputScheme == HostInputSchemeTouch) {
    // disabled
    return;
  }

  // if ([_scene containsTouchPoint:[sender locationInView:self.view]]) {
  //   [Utility middleClickOn:_clientToHostProxy at:_scene.mousePosition];
  // }
}

- (IBAction)threeFingerPanGestureTriggered:(UIPanGestureRecognizer*)sender {
  // LOG_TRACE(INFO) << "threeFingerPanGestureTriggered";
  // if ([sender state] == UIGestureRecognizerStateChanged) {
  //   CGPoint translation = [sender translationInView:self.view];
  //   if (translation.y > 0) {
  //     // Swiped down - do nothing
  //   } else if (translation.y < 0) {
  //     // Swiped up
  //     [_keyEntryView becomeFirstResponder];
  //   }
  //   [sender setTranslation:CGPointZero inView:self.view];
  // }
}

- (IBAction)edgeGestureTriggered:(UIScreenEdgePanGestureRecognizer*)sender {
  // LOG_TRACE(INFO) << "edgeGestureTriggered";
}

- (IBAction)swipeGestureTriggered:(UISwipeGestureRecognizer*)sender {
  // LOG_TRACE(INFO) << "swipeGestureTriggered";
}

#pragma mark - UIGestureRecognizerDelegate

// Allow panning and zooming to occur simultaneously.
// Allow panning and long press to occur simultaneously.
// Pinch requires 2 touches, and long press requires a single touch, so they are
// mutually exclusive regardless of if panning is the initiating gesture.
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:
        (UIGestureRecognizer*)otherGestureRecognizer {
  if (gestureRecognizer == _pinchRecognizer ||
      (gestureRecognizer == _panRecognizer)) {
    if (otherGestureRecognizer == _pinchRecognizer ||
        otherGestureRecognizer == _panRecognizer) {
      return YES;
    }
  }

  if (gestureRecognizer == _flingRecognizer ||
      (gestureRecognizer == _panRecognizer)) {
    if (otherGestureRecognizer == _flingRecognizer ||
        otherGestureRecognizer == _panRecognizer) {
      return YES;
    }
  }

  if (gestureRecognizer == _longPressRecognizer ||
      gestureRecognizer == _panRecognizer) {
    if (otherGestureRecognizer == _longPressRecognizer ||
        otherGestureRecognizer == _panRecognizer) {
      return YES;
    }
  }

  if (gestureRecognizer == _twoFingerTapRecognizer &&
      otherGestureRecognizer == _longPressRecognizer) {
    return YES;
  }

  if (gestureRecognizer == _panRecognizer &&
      otherGestureRecognizer == _edgeGesture) {
    return YES;
  }
  // TODO(nicholss): If we return NO here, it dismisses the other reconizers.
  // As we add more types of reconizers, they need to be accounted for in the
  // above logic.
  return NO;
}

@end
