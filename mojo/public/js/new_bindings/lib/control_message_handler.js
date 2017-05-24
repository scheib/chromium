// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {
  var internal = mojo.internal;

  function validateControlRequestWithResponse(message) {
    var messageValidator = new internal.Validator(message);
    var error = messageValidator.validateMessageIsRequestExpectingResponse();
    if (error !== internal.validationError.NONE) {
      throw error;
    }

    if (message.getName() != mojo.interfaceControl2.kRunMessageId) {
      throw new Error("Control message name is not kRunMessageId");
    }

    // Validate payload.
    error = mojo.interfaceControl2.RunMessageParams.validate(messageValidator,
        message.getHeaderNumBytes());
    if (error != internal.validationError.NONE) {
      throw error;
    }
  }

  function validateControlRequestWithoutResponse(message) {
    var messageValidator = new internal.Validator(message);
    var error = messageValidator.validateMessageIsRequestWithoutResponse();
    if (error != internal.validationError.NONE) {
      throw error;
    }

    if (message.getName() != mojo.interfaceControl2.kRunOrClosePipeMessageId) {
      throw new Error("Control message name is not kRunOrClosePipeMessageId");
    }

    // Validate payload.
    error = mojo.interfaceControl2.RunOrClosePipeMessageParams.validate(
        messageValidator, message.getHeaderNumBytes());
    if (error != internal.validationError.NONE) {
      throw error;
    }
  }

  function runOrClosePipe(message, interfaceVersion) {
    var reader = new internal.MessageReader(message);
    var runOrClosePipeMessageParams = reader.decodeStruct(
        mojo.interfaceControl2.RunOrClosePipeMessageParams);
    return interfaceVersion >=
        runOrClosePipeMessageParams.input.requireVersion.version;
  }

  function run(message, responder, interfaceVersion) {
    var reader = new internal.MessageReader(message);
    var runMessageParams =
        reader.decodeStruct(mojo.interfaceControl2.RunMessageParams);
    var runOutput = null;

    if (runMessageParams.input.queryVersion) {
      runOutput = new mojo.interfaceControl2.RunOutput();
      runOutput.queryVersionResult = new
          mojo.interfaceControl2.QueryVersionResult(
              {'version': interfaceVersion});
    }

    var runResponseMessageParams = new
        mojo.interfaceControl2.RunResponseMessageParams();
    runResponseMessageParams.output = runOutput;

    var messageName = mojo.interfaceControl2.kRunMessageId;
    var payloadSize =
        mojo.interfaceControl2.RunResponseMessageParams.encodedSize;
    var requestID = reader.requestID;
    var builder = new internal.MessageV1Builder(messageName,
        payloadSize, internal.kMessageIsResponse, requestID);
    builder.encodeStruct(mojo.interfaceControl2.RunResponseMessageParams,
                         runResponseMessageParams);
    responder.accept(builder.finish());
    return true;
  }

  function isInterfaceControlMessage(message) {
    return message.getName() == mojo.interfaceControl2.kRunMessageId ||
           message.getName() == mojo.interfaceControl2.kRunOrClosePipeMessageId;
  }

  function ControlMessageHandler(interfaceVersion) {
    this.interfaceVersion_ = interfaceVersion;
  }

  ControlMessageHandler.prototype.accept = function(message) {
    validateControlRequestWithoutResponse(message);
    return runOrClosePipe(message, this.interfaceVersion_);
  };

  ControlMessageHandler.prototype.acceptWithResponder = function(message,
      responder) {
    validateControlRequestWithResponse(message);
    return run(message, responder, this.interfaceVersion_);
  };

  internal.ControlMessageHandler = ControlMessageHandler;
  internal.isInterfaceControlMessage = isInterfaceControlMessage;
})();
