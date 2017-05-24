// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/peer_connection.h"

#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/system/core.h"

namespace mojo {
namespace edk {

PeerConnection::PeerConnection() = default;

PeerConnection::~PeerConnection() {
  if (is_connected_)
    internal::g_core->ClosePeerConnection(connection_id_);
}

ScopedMessagePipeHandle PeerConnection::Connect(ConnectionParams params) {
  DCHECK(!is_connected_);
  is_connected_ = true;

  ports::PortRef peer_port;
  auto pipe = internal::g_core->CreatePartialMessagePipe(&peer_port);
  connection_id_ =
      internal::g_core->ConnectToPeer(std::move(params), peer_port);
  return pipe;
}

}  // namespace edk
}  // namespace mojo
