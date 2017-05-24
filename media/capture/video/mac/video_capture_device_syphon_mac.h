// Copyright 2017 vade. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of VideoCaptureDevice class for Syphon Clients
// devices by using the Syphon SDK

#ifndef MEDIA_CAPTURE_VIDEO_MAC_VIDEO_CAPTURE_DEVICE_SYPHON_MAC_H_
#define MEDIA_CAPTURE_VIDEO_MAC_VIDEO_CAPTURE_DEVICE_SYPHON_MAC_H_

#include "media/capture/video/video_capture_device.h"

#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#include "third_party/Syphon/mac/Syphon.framework/Headers/Syphon.h"
#include <stddef.h>
#include <stdint.h>

#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"

namespace {
    class SyphonCaptureDelegate;
}  // namespace

namespace tracked_objects {
    class Location;
}  // namespace tracked_objects

namespace media {
    
    class CAPTURE_EXPORT VideoCaptureDeviceSyphonMac : public VideoCaptureDevice {
    public:
        static void EnumerateDevices(
                                     VideoCaptureDeviceDescriptors* device_descriptors);
        
        static void EnumerateDeviceCapabilities(
                                                const VideoCaptureDeviceDescriptor& descriptor,
                                                VideoCaptureFormats* supported_formats);
        
        explicit VideoCaptureDeviceSyphonMac(
                                               const VideoCaptureDeviceDescriptor& descriptor);
        ~VideoCaptureDeviceSyphonMac() override;
        
        // Copy of VideoCaptureDevice::Client::OnIncomingCapturedData().
        void OnIncomingCapturedData(const uint8_t* data,
                                    size_t length,
                                    const VideoCaptureFormat& frame_format,
                                    int rotation,  // Clockwise.
                                    base::TimeTicks reference_time,
                                    base::TimeDelta timestamp);
        
        // Forwarder to VideoCaptureDevice::Client::OnError().
        void SendErrorString(const tracked_objects::Location& from_here,
                             const std::string& reason);
        
        // Forwarder to VideoCaptureDevice::Client::OnLog().
        void SendLogString(const std::string& message);
        
    private:
        // VideoCaptureDevice implementation.
        void AllocateAndStart(
                              const VideoCaptureParams& params,
                              std::unique_ptr<VideoCaptureDevice::Client> client) override;
        void StopAndDeAllocate() override;
        
        // Protects concurrent setting and using of |client_|.
        base::Lock lock_;
        std::unique_ptr<VideoCaptureDevice::Client> client_;
        
        // Checks for Device (a.k.a. Audio) thread.
        base::ThreadChecker thread_checker_;
        
        SyphonClient* syphonClient;
        NSOpenGLContext* context;

        __block NSSize lastSize;
        // Todo: We create and destroy our FBO / Texture every frame
        // Lets cache them at some point.
        GLuint fbo;
        GLuint texture;

        // for Async Readback
        GLsizei numPBOs;
        GLsizei currentPBO;
        GLsizei numTransfers;
        GLuint* PBOs;
        size_t textureDataLength;        
        
        base::TimeTicks first_ref_time_;

        bool run;
        DISALLOW_COPY_AND_ASSIGN(VideoCaptureDeviceSyphonMac);
    };
    
}  // namespace media

#endif  // MEDIA_CAPTURE_VIDEO_MAC_VIDEO_CAPTURE_DEVICE_SYPHON_MAC_H_
