// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/ios/session/remoting_client.h"

#include <memory>

#import "base/mac/bind_objc_block.h"
#import "ios/third_party/material_components_ios/src/components/Dialogs/src/MaterialDialogs.h"
#import "remoting/ios/display/gl_display_handler.h"
#import "remoting/ios/domain/client_session_details.h"
#import "remoting/ios/domain/host_info.h"

#include "base/strings/sys_string_conversions.h"
#include "remoting/client/chromoting_client_runtime.h"
#include "remoting/client/chromoting_session.h"
#include "remoting/client/connect_to_host_info.h"
#include "remoting/client/input/keyboard_interpreter.h"
#include "remoting/client/ui/gesture_interpreter.h"
#include "remoting/client/ui/renderer_proxy.h"
#include "remoting/ios/session/remoting_client_session_delegate.h"
#include "remoting/protocol/session.h"
#include "remoting/protocol/video_renderer.h"

NSString* const kHostSessionStatusChanged = @"kHostSessionStatusChanged";
NSString* const kHostSessionPinProvided = @"kHostSessionPinProvided";

NSString* const kSessionDetails = @"kSessionDetails";
NSString* const kSessonStateErrorCode = @"kSessonStateErrorCode";
NSString* const kHostSessionPin = @"kHostSessionPin";

@interface RemotingClient () {
  remoting::ChromotingClientRuntime* _runtime;
  std::unique_ptr<remoting::ChromotingSession> _session;
  std::unique_ptr<remoting::RemotingClientSessonDelegate> _sessonDelegate;
  ClientSessionDetails* _sessionDetails;
  // Call _secretFetchedCallback on the network thread.
  remoting::protocol::SecretFetchedCallback _secretFetchedCallback;
  std::unique_ptr<remoting::RendererProxy> _renderer;
  std::unique_ptr<remoting::GestureInterpreter> _gestureInterpreter;
  std::unique_ptr<remoting::KeyboardInterpreter> _keyboardInterpreter;
}
@end

@implementation RemotingClient

@synthesize displayHandler = _displayHandler;

- (instancetype)init {
  self = [super init];
  if (self) {
    _runtime = remoting::ChromotingClientRuntime::GetInstance();
    _sessonDelegate.reset(new remoting::RemotingClientSessonDelegate(self));
    _sessionDetails = [[ClientSessionDetails alloc] init];

    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(hostSessionPinProvided:)
               name:kHostSessionPinProvided
             object:nil];
  }
  return self;
}

- (void)connectToHost:(HostInfo*)hostInfo
             username:(NSString*)username
          accessToken:(NSString*)accessToken {
  DCHECK(_runtime->ui_task_runner()->BelongsToCurrentThread());
  DCHECK(hostInfo);
  DCHECK(hostInfo.jabberId);
  DCHECK(hostInfo.hostId);
  DCHECK(hostInfo.publicKey);

  _sessionDetails.hostInfo = hostInfo;

  remoting::ConnectToHostInfo info;
  info.username = base::SysNSStringToUTF8(username);
  info.auth_token = base::SysNSStringToUTF8(accessToken);
  info.host_jid = base::SysNSStringToUTF8(hostInfo.jabberId);
  info.host_id = base::SysNSStringToUTF8(hostInfo.hostId);
  info.host_pubkey = base::SysNSStringToUTF8(hostInfo.publicKey);
  // TODO(nicholss): If iOS supports pairing, pull the stored data and
  // insert it here.
  info.pairing_id = "";
  info.pairing_secret = "";

  // TODO(nicholss): I am not sure about the following fields yet.
  // info.capabilities =
  // info.flags =
  // info.host_version =
  // info.host_os =
  // info.host_os_version =

  remoting::protocol::ClientAuthenticationConfig client_auth_config;
  client_auth_config.host_id = info.host_id;
  client_auth_config.pairing_client_id = info.pairing_id;
  client_auth_config.pairing_secret = info.pairing_secret;

  // ChromotingClient keeps strong reference to |client_auth_config| through its
  // lifetime.
  __weak RemotingClient* weakSelf = self;
  client_auth_config.fetch_secret_callback = base::BindBlockArc(
      ^(bool pairing_supported, const remoting::protocol::SecretFetchedCallback&
                                    secret_fetched_callback) {
        RemotingClient* strongSelf = weakSelf;
        if (!strongSelf) {
          return;
        }
        strongSelf->_secretFetchedCallback = secret_fetched_callback;
        strongSelf->_sessionDetails.state = SessionPinPrompt;
        [[NSNotificationCenter defaultCenter]
            postNotificationName:kHostSessionStatusChanged
                          object:weakSelf
                        userInfo:[NSDictionary
                                     dictionaryWithObject:strongSelf
                                                              ->_sessionDetails
                                                   forKey:kSessionDetails]];
      });

  // TODO(nicholss): Add audio support to iOS.
  base::WeakPtr<remoting::protocol::AudioStub> audioPlayer = nullptr;

  _displayHandler = [[GlDisplayHandler alloc] init];
  _displayHandler.delegate = self;

  _session.reset(new remoting::ChromotingSession(
      _sessonDelegate->GetWeakPtr(), [_displayHandler CreateCursorShapeStub],
      [_displayHandler CreateVideoRenderer], audioPlayer, info,
      client_auth_config));
  _renderer = [_displayHandler CreateRendererProxy];
  _gestureInterpreter.reset(
      new remoting::GestureInterpreter(_renderer.get(), _session.get()));
  _keyboardInterpreter.reset(new remoting::KeyboardInterpreter(_session.get()));

  _session->Connect();
}

- (void)disconnectFromHost {
  if (_session) {
    _session->Disconnect();
    _runtime->network_task_runner()->DeleteSoon(FROM_HERE, _session.release());
  }
  _displayHandler = nil;

  // This needs to be deleted on the display thread since GlDisplayHandler binds
  // its WeakPtrFactory to the display thread.
  // TODO(yuweih): Ideally this constraint can be removed once we allow
  // GlRenderer to be created on the UI thread before being used.
  if (_renderer) {
    _runtime->display_task_runner()->DeleteSoon(FROM_HERE, _renderer.release());
  }
}

#pragma mark - Eventing

- (void)hostSessionPinProvided:(NSNotification*)notification {
  NSString* pin = [[notification userInfo] objectForKey:kHostSessionPin];
  if (_secretFetchedCallback) {
    _runtime->network_task_runner()->PostTask(
        FROM_HERE, base::BindBlockArc(^{
          _secretFetchedCallback.Run(base::SysNSStringToUTF8(pin));
        }));
  }
}

#pragma mark - Properties

- (HostInfo*)hostInfo {
  return _sessionDetails.hostInfo;
}

- (remoting::GestureInterpreter*)gestureInterpreter {
  return _gestureInterpreter.get();
}

- (remoting::KeyboardInterpreter*)keyboardInterpreter {
  return _keyboardInterpreter.get();
}

#pragma mark - ChromotingSession::Delegate

- (void)onConnectionState:(remoting::protocol::ConnectionToHost::State)state
                    error:(remoting::protocol::ErrorCode)error {
  switch (state) {
    case remoting::protocol::ConnectionToHost::INITIALIZING:
      NSLog(@"State --> INITIALIZING");
      _sessionDetails.state = SessionInitializing;
      break;
    case remoting::protocol::ConnectionToHost::CONNECTING:
      NSLog(@"State --> CONNECTING");
      _sessionDetails.state = SessionConnecting;
      break;
    case remoting::protocol::ConnectionToHost::AUTHENTICATED:
      NSLog(@"State --> AUTHENTICATED");
      _sessionDetails.state = SessionAuthenticated;
      break;
    case remoting::protocol::ConnectionToHost::CONNECTED:
      NSLog(@"State --> CONNECTED");
      _sessionDetails.state = SessionConnected;
      break;
    case remoting::protocol::ConnectionToHost::FAILED:
      NSLog(@"State --> FAILED");
      _sessionDetails.state = SessionFailed;
      break;
    case remoting::protocol::ConnectionToHost::CLOSED:
      NSLog(@"State --> CLOSED");
      _sessionDetails.state = SessionClosed;
      break;
    default:
      LOG(ERROR) << "onConnectionState, unknown state: " << state;
  }

  // TODO(nicholss): Send along the error code when we know what to do about it.
  [[NSNotificationCenter defaultCenter]
      postNotificationName:kHostSessionStatusChanged
                    object:self
                  userInfo:[NSDictionary dictionaryWithObject:_sessionDetails
                                                       forKey:kSessionDetails]];
}

- (void)commitPairingCredentialsForHost:(NSString*)host
                                     id:(NSString*)id
                                 secret:(NSString*)secret {
  NSLog(@"TODO(nicholss): implement this, commitPairingCredentialsForHost.");
}

- (void)fetchThirdPartyTokenForUrl:(NSString*)tokenUrl
                          clientId:(NSString*)clientId
                             scope:(NSString*)scope {
  NSLog(@"TODO(nicholss): implement this, fetchThirdPartyTokenForUrl.");
}

- (void)setCapabilities:(NSString*)capabilities {
  NSLog(@"TODO(nicholss): implement this, setCapabilities.");
}

- (void)handleExtensionMessageOfType:(NSString*)type
                             message:(NSString*)message {
  NSLog(@"TODO(nicholss): implement this, handleExtensionMessageOfType %@:%@.",
        type, message);
}

- (void)surfaceChanged:(const CGRect&)frame {
  // Note that GLKView automatically sets the OpenGL viewport size to the size
  // of the surface.
  [_displayHandler onSurfaceChanged:frame];
  _gestureInterpreter->OnSurfaceSizeChanged(frame.size.width,
                                            frame.size.height);
}

#pragma mark - GlDisplayHandlerDelegate

- (void)canvasSizeChanged:(CGSize)size {
  _gestureInterpreter->OnDesktopSizeChanged(size.width, size.height);
}

- (void)rendererTicked {
  _gestureInterpreter->ProcessAnimations();
}

@end
