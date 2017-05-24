// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/ios/app/client_connection_view_controller.h"

#import "ios/third_party/material_components_ios/src/components/ActivityIndicator/src/MDCActivityIndicator.h"
#import "ios/third_party/material_components_ios/src/components/Buttons/src/MaterialButtons.h"
#import "ios/third_party/material_components_ios/src/components/NavigationBar/src/MaterialNavigationBar.h"
#import "remoting/ios/app/pin_entry_view.h"
#import "remoting/ios/domain/client_session_details.h"
#import "remoting/ios/session/remoting_client.h"

#include "base/strings/sys_string_conversions.h"
#include "remoting/protocol/client_authentication_config.h"

static const CGFloat kIconRadius = 30.f;
static const CGFloat kActivityIndicatorStrokeWidth = 3.f;
static const CGFloat kActivityIndicatorRadius = 33.f;

static const CGFloat kPinEntryViewWidth = 240.f;
static const CGFloat kPinEntryViewHeight = 90.f;

static const CGFloat kCenterShift = -80.f;
static const CGFloat kPadding = 20.f;
static const CGFloat kMargin = 20.f;

static const CGFloat kBarHeight = 58.f;

static const CGFloat kKeyboardAnimationTime = 0.3;

@interface ClientConnectionViewController ()<PinEntryDelegate> {
  UIImageView* _iconView;
  MDCActivityIndicator* _activityIndicator;
  UILabel* _statusLabel;
  MDCNavigationBar* _navBar;
  PinEntryView* _pinEntryView;
  NSString* _remoteHostName;
}
@end

@implementation ClientConnectionViewController

@synthesize state = _state;
@synthesize delegate = _delegate;

- (id)init {
  self = [super init];
  if (self) {
    self.navigationItem.rightBarButtonItem =
        [[UIBarButtonItem alloc] initWithTitle:@"CANCEL"
                                         style:UIBarButtonItemStylePlain
                                        target:self
                                        action:@selector(didTapCancel:)];

    _navBar = [[MDCNavigationBar alloc] initWithFrame:CGRectZero];
    [_navBar observeNavigationItem:self.navigationItem];

    [_navBar setBackgroundColor:[UIColor blackColor]];
    MDCNavigationBarTextColorAccessibilityMutator* mutator =
        [[MDCNavigationBarTextColorAccessibilityMutator alloc] init];
    [mutator mutate:_navBar];
    [self.view addSubview:_navBar];
    _navBar.translatesAutoresizingMaskIntoConstraints = NO;
    _remoteHostName = @"";
  }
  return self;
}

#pragma mark - UIViewController

- (void)loadView {
  [super loadView];

  self.view.backgroundColor = [UIColor blackColor];

  _activityIndicator = [[MDCActivityIndicator alloc] initWithFrame:CGRectZero];
  [self.view addSubview:_activityIndicator];

  _statusLabel = [[UILabel alloc] initWithFrame:CGRectZero];
  [self.view addSubview:_statusLabel];

  _iconView = [[UIImageView alloc] initWithFrame:CGRectZero];
  [self.view addSubview:_iconView];

  _pinEntryView = [[PinEntryView alloc] init];
  [self.view addSubview:_pinEntryView];
  _pinEntryView.delegate = self;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _iconView.contentMode = UIViewContentModeCenter;
  _iconView.alpha = 0.87f;
  _iconView.backgroundColor = UIColor.lightGrayColor;
  _iconView.layer.cornerRadius = kIconRadius;
  _iconView.layer.masksToBounds = YES;
  _iconView.image = [UIImage imageNamed:@"ic_desktop"];

  _activityIndicator.radius = kActivityIndicatorRadius;
  _activityIndicator.trackEnabled = YES;
  _activityIndicator.strokeWidth = kActivityIndicatorStrokeWidth;
  _activityIndicator.cycleColors = @[ [UIColor whiteColor] ];

  _statusLabel.numberOfLines = 1;
  _statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
  _statusLabel.textColor = [UIColor whiteColor];
  _statusLabel.textAlignment = NSTextAlignmentCenter;

  _pinEntryView.hidden = YES;
}

- (void)viewWillLayoutSubviews {
  [super viewWillLayoutSubviews];

  _navBar.frame = CGRectMake(0.f, 0.f, self.view.frame.size.width, kBarHeight);
  [_navBar setNeedsLayout];

  _iconView.frame = CGRectMake(0, 0, kIconRadius * 2, kIconRadius * 2.f);
  _iconView.center =
      CGPointMake(self.view.center.x, self.view.center.y + kCenterShift);

  [_activityIndicator sizeToFit];
  _activityIndicator.center = _iconView.center;

  _statusLabel.frame =
      CGRectMake(kMargin, _activityIndicator.center.y + kIconRadius + kPadding,
                 self.view.frame.size.width - kMargin * 2.f,
                 _statusLabel.font.pointSize * _statusLabel.numberOfLines);

  _pinEntryView.frame = CGRectMake(
      (self.view.frame.size.width - kPinEntryViewWidth) / 2.f,
      _statusLabel.frame.origin.y + _statusLabel.frame.size.height + kPadding,
      kPinEntryViewWidth, kPinEntryViewHeight);
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self.navigationController setNavigationBarHidden:YES animated:animated];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(hostSessionStatusChanged:)
             name:kHostSessionStatusChanged
           object:nil];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(keyboardWillShow:)
             name:UIKeyboardWillShowNotification
           object:nil];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(keyboardWillHide:)
             name:UIKeyboardWillHideNotification
           object:nil];

  [_activityIndicator startAnimating];
}

- (void)viewWillDisappear:(BOOL)animated {
  [super viewWillDisappear:animated];
  [_activityIndicator stopAnimating];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma mark - Keyboard

- (void)keyboardWillShow:(NSNotification*)notification {
  CGSize keyboardSize =
      [[[notification userInfo] objectForKey:UIKeyboardFrameEndUserInfoKey]
          CGRectValue]
          .size;

  [UIView
      animateWithDuration:kKeyboardAnimationTime
               animations:^{
                 CGRect f = self.view.frame;
                 CGFloat newHeight =
                     self.view.frame.size.height - keyboardSize.height;
                 CGFloat overlap =
                     newHeight - (_pinEntryView.frame.origin.y +
                                  _pinEntryView.frame.size.height + kPadding);
                 if (overlap < 0) {
                   f.origin.y = overlap;
                   self.view.frame = f;
                 }
               }];
}

- (void)keyboardWillHide:(NSNotification*)notification {
  [UIView animateWithDuration:kKeyboardAnimationTime
                   animations:^{
                     CGRect f = self.view.frame;
                     f.origin.y = 0.f;
                     self.view.frame = f;
                   }];
}

#pragma mark - Properties

- (void)setState:(ClientConnectionViewState)state {
  _state = state;
  switch (_state) {
    case ClientViewConnecting:
      [self showConnectingState];
      break;
    case ClientViewPinPrompt:
      [self showPinPromptState];
      break;
    case ClientViewConnected:
      [self showConnectedState];
      break;
    case ClientViewClosed:
      [self dismissViewControllerAnimated:YES completion:nil];
      break;
  }
}

- (void)setDelegate:(id<ClientConnectionViewControllerDelegate>)delegate {
  _delegate = delegate;
  if (_delegate) {
    _remoteHostName = [_delegate getConnectingHostName];
    // To get the view to use the new remote host name.
    [self setState:_state];
  }
}

#pragma mark - Private

- (void)showConnectingState {
  [_pinEntryView endEditing:YES];
  _statusLabel.text =
      [NSString stringWithFormat:@"Connecting to %@", _remoteHostName];
  [_activityIndicator stopAnimating];
  _activityIndicator.cycleColors = @[ [UIColor whiteColor] ];
  _activityIndicator.indicatorMode = MDCActivityIndicatorModeIndeterminate;
  _activityIndicator.hidden = NO;
  _pinEntryView.hidden = YES;
  [_activityIndicator startAnimating];
}

- (void)showPinPromptState {
  _statusLabel.text = [NSString stringWithFormat:@"%@", _remoteHostName];
  [_activityIndicator stopAnimating];
  _activityIndicator.hidden = YES;
  _pinEntryView.hidden = NO;
  [_pinEntryView becomeFirstResponder];
}

- (void)showConnectedState {
  [_pinEntryView endEditing:YES];
  _statusLabel.text =
      [NSString stringWithFormat:@"Connected to %@", _remoteHostName];
  _activityIndicator.progress = 0.0;
  _pinEntryView.hidden = YES;
  _activityIndicator.hidden = NO;
  _activityIndicator.indicatorMode = MDCActivityIndicatorModeDeterminate;
  _activityIndicator.cycleColors = @[ [UIColor greenColor] ];
  [_activityIndicator startAnimating];
  _activityIndicator.progress = 1.0;
  [self dismissViewControllerAnimated:YES
                           completion:^{
                             [_delegate clientConnected];
                           }];
}

- (void)didProvidePin:(NSString*)pin createPairing:(BOOL)createPairing {
  // TODO(nicholss): There is an open question if createPairing is supported on
  // iOS. Need to fingure this out.
  [[NSNotificationCenter defaultCenter]
      postNotificationName:kHostSessionPinProvided
                    object:self
                  userInfo:[NSDictionary dictionaryWithObject:pin
                                                       forKey:kHostSessionPin]];
}

- (void)didTapCancel:(id)sender {
  NSLog(@"%@ was tapped.", NSStringFromClass([sender class]));
  // TODO(nicholss): Need to cancel the pending connection.
  [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)hostSessionStatusChanged:(NSNotification*)notification {
  ClientConnectionViewState state;
  ClientSessionDetails* sessionDetails =
      [[notification userInfo] objectForKey:kSessionDetails];
  switch (sessionDetails.state) {
    case SessionInitializing:
    // Same as HostConnecting in UI. Fall-though.
    case SessionAuthenticated:
    // Same as HostConnecting in UI. Fall-though.
    case SessionConnecting:
      state = ClientViewConnecting;
      break;
    case SessionPinPrompt:
      state = ClientViewPinPrompt;
      break;
    case SessionConnected:
      state = ClientViewConnected;
      break;
    case SessionFailed:
    // TODO(nicholss): Implement an error screen.
    case SessionClosed:
      state = ClientViewClosed;
      break;
    default:
      LOG(ERROR) << "Unknown State for Session, " << sessionDetails.state;
      return;
  }
  [[NSOperationQueue mainQueue] addOperationWithBlock:^{
    [self setState:state];
  }];
}

@end
