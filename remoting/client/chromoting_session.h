// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_CHROMOTING_SESSION_H_
#define REMOTING_CLIENT_CHROMOTING_SESSION_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "remoting/client/chromoting_client.h"
#include "remoting/client/client_context.h"
#include "remoting/client/client_telemetry_logger.h"
#include "remoting/client/client_user_interface.h"
#include "remoting/client/connect_to_host_info.h"
#include "remoting/client/input/client_input_injector.h"
#include "remoting/proto/control.pb.h"
#include "remoting/proto/event.pb.h"
#include "remoting/protocol/clipboard_stub.h"
#include "remoting/protocol/cursor_shape_stub.h"
#include "remoting/signaling/xmpp_signal_strategy.h"

namespace remoting {

namespace protocol {
class AudioStub;
class ClipboardEvent;
class PerformanceTracker;
class VideoRenderer;
}  // namespace protocol

class ChromotingClientRuntime;

// ChromotingSession is scoped to the session.
// This class is Created on the UI thread but thereafter it is used and
// destroyed on the network thread. Except where indicated, all methods are
// called on the network thread.
class ChromotingSession : public ClientUserInterface,
                          public protocol::ClipboardStub,
                          public ClientInputInjector {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Notifies Java code of the current connection status.
    virtual void OnConnectionState(protocol::ConnectionToHost::State state,
                                   protocol::ErrorCode error) = 0;

    // Saves new pairing credentials to permanent storage.
    virtual void CommitPairingCredentials(const std::string& host,
                                          const std::string& id,
                                          const std::string& secret) = 0;

    // Pops up a third party login page to fetch token required for
    // authentication.
    virtual void FetchThirdPartyToken(const std::string& token_url,
                                      const std::string& client_id,
                                      const std::string& scope) = 0;

    // Pass on the set of negotiated capabilities to the client.
    virtual void SetCapabilities(const std::string& capabilities) = 0;

    // Passes on the deconstructed ExtensionMessage to the client to handle
    // appropriately.
    virtual void HandleExtensionMessage(const std::string& type,
                                        const std::string& message) = 0;
  };

  // Initiates a connection with the specified host. Call from the UI thread.
  ChromotingSession(
      base::WeakPtr<ChromotingSession::Delegate> delegate,
      std::unique_ptr<protocol::CursorShapeStub> cursor_stub,
      std::unique_ptr<protocol::VideoRenderer> video_renderer,
      base::WeakPtr<protocol::AudioStub> audio_player,
      const ConnectToHostInfo& info,
      const protocol::ClientAuthenticationConfig& client_auth_config);

  ~ChromotingSession() override;

  // Starts the connection. Can be called on any thread.
  void Connect();

  // Terminates the current connection (if it hasn't already failed) and cleans
  // up. The instance will no longer be valid after calling this function.
  // Must be called before destruction.
  void Disconnect();

  // Requests the android app to fetch a third-party token.
  void FetchThirdPartyToken(
      const std::string& host_public_key,
      const std::string& token_url,
      const std::string& scope,
      const protocol::ThirdPartyTokenFetchedCallback& token_fetched_callback);

  // Called by the android app when the token is fetched.
  void HandleOnThirdPartyTokenFetched(const std::string& token,
                                      const std::string& shared_secret);

  // Provides the user's PIN and resumes the host authentication attempt. Call
  // on the UI thread once the user has finished entering this PIN into the UI,
  // but only after the UI has been asked to provide a PIN (via FetchSecret()).
  void ProvideSecret(const std::string& pin,
                     bool create_pair,
                     const std::string& device_name);

  // Moves the host's cursor to the specified coordinates, optionally with some
  // mouse button depressed. If |button| is BUTTON_UNDEFINED, no click is made.
  void SendMouseEvent(int x,
                      int y,
                      protocol::MouseEvent_MouseButton button,
                      bool button_down);
  void SendMouseWheelEvent(int delta_x, int delta_y);

  //  ClientInputInjector implementation.
  bool SendKeyEvent(int scan_code, int key_code, bool key_down) override;
  void SendTextEvent(const std::string& text) override;

  // Sends the provided touch event payload to the host.
  void SendTouchEvent(const protocol::TouchEvent& touch_event);

  // Enables or disables the video channel. May be called from any thread.
  void EnableVideoChannel(bool enable);

  void SendClientMessage(const std::string& type, const std::string& data);

  // ClientUserInterface implementation.
  void OnConnectionState(protocol::ConnectionToHost::State state,
                         protocol::ErrorCode error) override;
  void OnConnectionReady(bool ready) override;
  void OnRouteChanged(const std::string& channel_name,
                      const protocol::TransportRoute& route) override;
  void SetCapabilities(const std::string& capabilities) override;
  void SetPairingResponse(const protocol::PairingResponse& response) override;
  void DeliverHostMessage(const protocol::ExtensionMessage& message) override;
  void SetDesktopSize(const webrtc::DesktopSize& size,
                      const webrtc::DesktopVector& dpi) override;
  protocol::ClipboardStub* GetClipboardStub() override;
  protocol::CursorShapeStub* GetCursorShapeStub() override;

  // CursorShapeStub implementation.
  void InjectClipboardEvent(const protocol::ClipboardEvent& event) override;

  // Get the weak pointer of the instance. Please only use it on the network
  // thread.
  base::WeakPtr<ChromotingSession> GetWeakPtr();

 private:
  void ConnectToHostOnNetworkThread();

  // Sets the device name. Can be called on any thread.
  void SetDeviceName(const std::string& device_name);

  void SendKeyEventInternal(int usb_key_code, bool key_down);

  // Enables or disables periodic logging of performance statistics. Called on
  // the network thread.
  void EnableStatsLogging(bool enabled);

  // If logging is enabled, logs the current connection statistics, and
  // triggers another call to this function after the logging time interval.
  // Called on the network thread.
  void LogPerfStats();

  // Releases the resource in the right order.
  void ReleaseResources();

  // Used to obtain task runner references.
  ChromotingClientRuntime* runtime_;

  // Called on UI thread.
  base::WeakPtr<ChromotingSession::Delegate> delegate_;
  ConnectToHostInfo connection_info_;
  protocol::ClientAuthenticationConfig client_auth_config_;

  // This group of variables is to be used on the network thread.
  std::unique_ptr<ClientContext> client_context_;
  std::unique_ptr<protocol::PerformanceTracker> perf_tracker_;
  std::unique_ptr<protocol::CursorShapeStub> cursor_shape_stub_;
  std::unique_ptr<protocol::VideoRenderer> video_renderer_;
  std::unique_ptr<ChromotingClient> client_;
  XmppSignalStrategy::XmppServerConfig xmpp_config_;
  std::unique_ptr<XmppSignalStrategy> signaling_;  // Must outlive client_
  protocol::ThirdPartyTokenFetchedCallback third_party_token_fetched_callback_;

  // Called on UI Thread.
  base::WeakPtr<protocol::AudioStub> audio_player_;

  // Indicates whether to establish a new pairing with this host. This is
  // modified in ProvideSecret(), but thereafter to be used only from the
  // network thread. (This is safe because ProvideSecret() is invoked at most
  // once per run, and always before any reference to this flag.)
  bool create_pairing_ = false;

  // The device name to appear in the paired-clients list. Accessed on the
  // network thread.
  std::string device_name_;

  // If this is true, performance statistics will be periodically written to
  // the Android log. Used on the network thread.
  bool stats_logging_enabled_ = false;

  // The set of capabilities supported by the client. Accessed on the network
  // thread. Once SetCapabilities() is called, this will contain the negotiated
  // set of capabilities for this remoting session.
  std::string capabilities_;

  // Indicates whether the client is connected to the host. Used on network
  // thread.
  bool connected_ = false;

  std::unique_ptr<ClientTelemetryLogger> logger_;

  base::WeakPtr<ChromotingSession> weak_ptr_;
  base::WeakPtrFactory<ChromotingSession> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ChromotingSession);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_CHROMOTING_SESSION_H_
