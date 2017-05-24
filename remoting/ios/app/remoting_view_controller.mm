// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/ios/app/remoting_view_controller.h"

#import "base/mac/bind_objc_block.h"
#import "ios/third_party/material_components_ios/src/components/AnimationTiming/src/MaterialAnimationTiming.h"
#import "ios/third_party/material_components_ios/src/components/AppBar/src/MaterialAppBar.h"
#import "ios/third_party/material_components_ios/src/components/Dialogs/src/MaterialDialogs.h"
#import "ios/third_party/material_components_ios/src/components/Snackbar/src/MaterialSnackbar.h"
#import "remoting/ios/app/client_connection_view_controller.h"
#import "remoting/ios/app/host_collection_view_controller.h"
#import "remoting/ios/app/host_view_controller.h"
#import "remoting/ios/app/remoting_settings_view_controller.h"
#import "remoting/ios/domain/client_session_details.h"
#import "remoting/ios/facade/remoting_authentication.h"
#import "remoting/ios/facade/remoting_service.h"
#import "remoting/ios/session/remoting_client.h"

#include "base/strings/sys_string_conversions.h"
#include "remoting/base/oauth_token_getter.h"
#include "remoting/client/connect_to_host_info.h"

static CGFloat kHostInset = 5.f;

@interface RemotingViewController ()<HostCollectionViewControllerDelegate,
                                     ClientConnectionViewControllerDelegate,
                                     UIViewControllerAnimatedTransitioning,
                                     UIViewControllerTransitioningDelegate> {
  bool _isAuthenticated;
  MDCDialogTransitionController* _dialogTransitionController;
  MDCAppBar* _appBar;
  HostCollectionViewController* _collectionViewController;
  RemotingService* _remotingService;
  RemotingClient* _client;
}
@end

// TODO(nicholss): Localize this file.
// TODO(nicholss): This file is not finished with integration, the app flow is
// still pending development.

@implementation RemotingViewController

- (instancetype)init {
  _isAuthenticated = NO;
  UICollectionViewFlowLayout* layout =
      [[UICollectionViewFlowLayout alloc] init];
  layout.minimumInteritemSpacing = 0;
  CGFloat sectionInset = kHostInset * 2.f;
  [layout setSectionInset:UIEdgeInsetsMake(sectionInset, sectionInset,
                                           sectionInset, sectionInset)];
  HostCollectionViewController* collectionVC = [
      [HostCollectionViewController alloc] initWithCollectionViewLayout:layout];
  self = [super initWithContentViewController:collectionVC];
  if (self) {
    _remotingService = [RemotingService SharedInstance];

    _collectionViewController = collectionVC;
    _collectionViewController.flexHeaderContainerViewController = self;
    _collectionViewController.delegate = self;

    _appBar = [[MDCAppBar alloc] init];
    [self addChildViewController:_appBar.headerViewController];

    _appBar.headerViewController.headerView.backgroundColor =
        [UIColor clearColor];
    _appBar.navigationBar.tintColor = [UIColor whiteColor];

    UIBarButtonItem* menuButton =
        [[UIBarButtonItem alloc] initWithImage:[UIImage imageNamed:@"Settings"]
                                         style:UIBarButtonItemStyleDone
                                        target:self
                                        action:@selector(didSelectSettings)];
    self.navigationItem.leftBarButtonItem = menuButton;

    UIBarButtonItem* refreshButton =
        [[UIBarButtonItem alloc] initWithTitle:@"Refresh"
                                         style:UIBarButtonItemStyleDone
                                        target:self
                                        action:@selector(didSelectRefresh)];
    self.navigationItem.rightBarButtonItem = refreshButton;

    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(hostSessionStatusChanged:)
               name:kHostSessionStatusChanged
             object:nil];
  }
  return self;
}

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  [_appBar addSubviewsToParent];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(hostsDidUpdateNotification:)
             name:kHostsDidUpdate
           object:nil];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(userDidUpdateNotification:)
             name:kUserDidUpdate
           object:nil];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];

  [self nowAuthenticated:_remotingService.authentication.user.isAuthenticated];
  [self presentStatus];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  if (!_isAuthenticated) {
    // TODO(nicholss): This is used as a demo of the app functionality for the
    // moment but the real app will force the login flow if unauthenticated.
    [self didSelectSettings];
    // [self didSelectRefresh];
    MDCSnackbarMessage* message = [[MDCSnackbarMessage alloc] init];
    message.text = @"Please login.";
    [MDCSnackbarManager showMessage:message];
  } else {
    [_remotingService requestHostListFetch];
  }
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];

  // Adjust the collection view's position and size so that it doesn't get
  // overlayed by the navigation bar.
  CGFloat collectionOffsetY =
      _appBar.headerViewController.headerView.frame.size.height;
  CGFloat collectionHeight = self.view.bounds.size.height - collectionOffsetY;
  CGRect oldFrame = _collectionViewController.collectionView.frame;
  _collectionViewController.collectionView.frame =
      CGRectMake(oldFrame.origin.x, collectionOffsetY, oldFrame.size.width,
                 collectionHeight);
}

#pragma mark - Remoting Service Notifications

- (void)hostsDidUpdateNotification:(NSNotification*)notification {
  [_collectionViewController.collectionView reloadData];
}

- (void)userDidUpdateNotification:(NSNotification*)notification {
  [self nowAuthenticated:_remotingService.authentication.user.isAuthenticated];
}

#pragma mark - RemotingAuthenticationDelegate

- (void)nowAuthenticated:(BOOL)authenticated {
  if (authenticated) {
    MDCSnackbarMessage* message = [[MDCSnackbarMessage alloc] init];
    message.text = @"Logged In!";
    [MDCSnackbarManager showMessage:message];
  } else {
    MDCSnackbarMessage* message = [[MDCSnackbarMessage alloc] init];
    message.text = @"Not logged in.";
    [MDCSnackbarManager showMessage:message];
  }
  _isAuthenticated = authenticated;
  [_collectionViewController.collectionView reloadData];
}

#pragma mark - RemotingHostListDelegate

// TODO(nicholss): these need to be a stats change like "none, loading,
// updated"...
- (void)hostListUpdated {
  [_collectionViewController.collectionView reloadData];
}

#pragma mark - ClientConnectionViewControllerDelegate

- (void)clientConnected {
  HostViewController* hostViewController =
      [[HostViewController alloc] initWithClient:_client];
  _client = nil;
  [self presentViewController:hostViewController animated:YES completion:nil];
}

- (NSString*)getConnectingHostName {
  if (_client) {
    return _client.hostInfo.hostName;
  }
  return nil;
}

#pragma mark - HostCollectionViewControllerDelegate

- (void)didSelectCell:(HostCollectionViewCell*)cell
           completion:(void (^)())completionBlock {
  _client = [[RemotingClient alloc] init];

  [_remotingService.authentication
      callbackWithAccessToken:base::BindBlockArc(^(
                                  remoting::OAuthTokenGetter::Status status,
                                  const std::string& user_email,
                                  const std::string& access_token) {
        // TODO(nicholss): Check status.
        HostInfo* hostInfo = cell.hostInfo;
        [_client connectToHost:hostInfo
                      username:base::SysUTF8ToNSString(user_email)
                   accessToken:base::SysUTF8ToNSString(access_token)];
      })];

  ClientConnectionViewController* clientConnectionViewController =
      [[ClientConnectionViewController alloc] init];
  clientConnectionViewController.delegate = self;
  [self presentViewController:clientConnectionViewController
                     animated:YES
                   completion:nil];
  completionBlock();
}

- (NSInteger)getHostCount {
  return _remotingService.hosts.count;
}

- (HostInfo*)getHostAtIndexPath:(NSIndexPath*)path {
  return _remotingService.hosts[path.row];
}

#pragma mark - UIViewControllerTransitioningDelegate

- (nullable id<UIViewControllerAnimatedTransitioning>)
animationControllerForPresentedController:(UIViewController*)presented
                     presentingController:(UIViewController*)presenting
                         sourceController:(UIViewController*)source {
  // TODO(nicholss): Not implemented yet.
  return nil;
}

- (nullable id<UIViewControllerAnimatedTransitioning>)
animationControllerForDismissedController:(UIViewController*)dismissed {
  return self;
}

#pragma mark - UIViewControllerAnimatedTransitioning

- (void)animateTransition:
    (id<UIViewControllerContextTransitioning>)transitionContext {
}

- (NSTimeInterval)transitionDuration:
    (id<UIViewControllerContextTransitioning>)transitionContext {
  return 0.2;
}

#pragma mark - Private

- (void)hostSessionStatusChanged:(NSNotification*)notification {
  NSLog(@"hostSessionStatusChanged: %@", [notification userInfo]);
}

- (void)closeViewController {
  [self dismissViewControllerAnimated:true completion:nil];
}

- (void)didSelectRefresh {
  // TODO(nicholss): Might want to rate limit this. Maybe remoting service
  // controls that.
  [_remotingService requestHostListFetch];
}

- (void)didSelectSettings {
  RemotingSettingsViewController* settingsViewController =
      [[RemotingSettingsViewController alloc] init];
  [self presentViewController:settingsViewController
                     animated:YES
                   completion:nil];
}

- (void)presentStatus {
  MDCSnackbarMessage* message = [[MDCSnackbarMessage alloc] init];
  if (_isAuthenticated) {
    message.text = [NSString
        stringWithFormat:@"Currently signed in as %@.",
                         _remotingService.authentication.user.userEmail];
    [MDCSnackbarManager showMessage:message];
  }
}

@end
