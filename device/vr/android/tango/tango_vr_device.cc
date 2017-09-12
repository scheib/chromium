// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/android/tango/tango_vr_device.h"

#include "tango_support.h"

#include "base/trace_event/trace_event.h"

#include "TangoHandler.h"

#define THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API -1

using base::android::AttachCurrentThread;
using tango_chromium::TangoHandler;
using tango_chromium::Hit;
using tango_chromium::Plane;

const float RAD_2_DEG = 180.0 / M_PI;

namespace device {

TangoVRDevice::TangoVRDevice(TangoVRDeviceProvider* provider)
    : tangoVRDeviceProvider(provider) {
  tangoCoordinateFramePair.base = TANGO_COORDINATE_FRAME_START_OF_SERVICE;
  tangoCoordinateFramePair.target = TANGO_COORDINATE_FRAME_DEVICE;
}

TangoVRDevice::~TangoVRDevice() {
}

mojom::VRDisplayInfoPtr TangoVRDevice::GetVRDevice() {
  TRACE_EVENT0("input", "TangoVRDevice::GetVRDevice");
  mojom::VRDisplayInfoPtr device = mojom::VRDisplayInfo::New();

  device->displayName = "Tango VR Device";

  device->capabilities = mojom::VRDisplayCapabilities::New();
  device->capabilities->hasOrientation = true;
  device->capabilities->hasPosition = true;
  device->capabilities->hasExternalDisplay = false;
  device->capabilities->canPresent = false;
  device->capabilities->hasPassThroughCamera = true;

  device->leftEye = mojom::VREyeParameters::New();
  device->rightEye = mojom::VREyeParameters::New();
  mojom::VREyeParametersPtr& left_eye = device->leftEye;
  mojom::VREyeParametersPtr& right_eye = device->rightEye;
  left_eye->fieldOfView = mojom::VRFieldOfView::New();
  right_eye->fieldOfView = mojom::VRFieldOfView::New();

  left_eye->offset.resize(3);
  right_eye->offset.resize(3);

  TangoHandler* tangoHandler = TangoHandler::getInstance();
  if (!tangoHandler->isConnected()) {
    // We may not be able to get an instance of TangoHandler right away, so
    // stub in some data till we have one.
    left_eye->fieldOfView->upDegrees = 45;
    left_eye->fieldOfView->downDegrees = 45;
    left_eye->fieldOfView->leftDegrees = 45;
    left_eye->fieldOfView->rightDegrees = 45;
    right_eye->fieldOfView->upDegrees = 45;
    right_eye->fieldOfView->downDegrees = 45;
    right_eye->fieldOfView->leftDegrees = 45;
    right_eye->fieldOfView->rightDegrees = 45;

    left_eye->offset[0] = -0.0;
    left_eye->offset[1] = -0.0;
    left_eye->offset[2] = -0.0;

    right_eye->offset[0] = 0.0;
    right_eye->offset[1] = 0.0;
    right_eye->offset[2] = 0.0;

    left_eye->renderWidth = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API/*screen_size[0]*/ / 2.0;
    left_eye->renderHeight = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API/*screen_size[1]*/;

    right_eye->renderWidth = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API/*screen_size[0]*/ / 2.0;
    right_eye->renderHeight = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API/*screen_size[1]*/;

    return device;
  }

  uint32_t iw = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API;
  uint32_t ih = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API;
  double fx = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API;
  double fy = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API;
  double cx = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API;
  double cy = THIS_VALUE_NEEDS_TO_BE_OBTAINED_FROM_THE_TANGO_API;

  tangoHandler->getCameraImageSize(&iw, &ih);
  tangoHandler->getCameraFocalLength(&fx, &fy);
  tangoHandler->getCameraPoint(&cx, &cy);

  float vDegrees = atan(ih / (2.0 * fy)) * RAD_2_DEG;
  float hDegrees = atan(iw / (2.0 * fx)) * RAD_2_DEG;

  left_eye->fieldOfView->upDegrees = vDegrees;
  left_eye->fieldOfView->downDegrees = vDegrees;
  left_eye->fieldOfView->leftDegrees = hDegrees;
  left_eye->fieldOfView->rightDegrees = hDegrees;

  right_eye->fieldOfView->upDegrees = vDegrees;
  right_eye->fieldOfView->downDegrees = vDegrees;
  right_eye->fieldOfView->leftDegrees = hDegrees;
  right_eye->fieldOfView->rightDegrees = hDegrees;

  left_eye->offset[0] = 0.0f;
  left_eye->offset[1] = 0.0f;
  left_eye->offset[2] = 0.0f;

  right_eye->offset[0] = 0.0f;
  right_eye->offset[1] = 0.0f;
  right_eye->offset[2] = 0.0f;

  left_eye->renderWidth = iw;
  left_eye->renderHeight = ih;

  right_eye->renderWidth = iw;
  right_eye->renderHeight = ih;

  // Store the orientation values so we can check in future GetPose()
  // calls if we need to update camera intrinsics and regenerate the
  // VRDeviceInfoPtr
  lastSensorOrientation = tangoHandler->getSensorOrientation();
  lastActivityOrientation = tangoHandler->getActivityOrientation();

  return device;
}

mojom::VRPosePtr TangoVRDevice::GetPose() {

  // Check to see if orientation has changed, and if so, fire
  // an OnChanged() so that the VRFieldOfView can be updated,
  // with the up-to-date VRDeviceInfoPtr sent to WebKit for correct
  // projection matrix calculations.
  TangoHandler* tangoHandler = TangoHandler::getInstance();
  if (tangoHandler->isConnected() &&
      (lastSensorOrientation  != tangoHandler->getSensorOrientation() ||
      lastActivityOrientation != tangoHandler->getActivityOrientation())) {
    VRDevice::OnChanged();
  }

  TangoPoseData tangoPoseData;

  mojom::VRPosePtr pose = nullptr;

  if (tangoHandler->isConnected() && tangoHandler->getPose(&tangoPoseData))
  {
    pose = mojom::VRPose::New();

    pose->timestamp = base::Time::Now().ToJsTime();

    pose->orientation.emplace(4);
    pose->position.emplace(3);

    pose->orientation.value()[0] = tangoPoseData.orientation[0]/*decomposed_transform.quaternion[0]*/;
    pose->orientation.value()[1] = tangoPoseData.orientation[1]/*decomposed_transform.quaternion[1]*/;
    pose->orientation.value()[2] = tangoPoseData.orientation[2]/*decomposed_transform.quaternion[2]*/;
    pose->orientation.value()[3] = tangoPoseData.orientation[3]/*decomposed_transform.quaternion[3]*/;

    pose->position.value()[0] = tangoPoseData.translation[0]/*decomposed_transform.translate[0]*/;
    pose->position.value()[1] = tangoPoseData.translation[1]/*decomposed_transform.translate[1]*/;
    pose->position.value()[2] = tangoPoseData.translation[2]/*decomposed_transform.translate[2]*/;
  }

  return pose;
}

void TangoVRDevice::ResetPose() {
  TangoHandler::getInstance()->resetPose();
}

mojom::VRPassThroughCameraPtr TangoVRDevice::GetPassThroughCamera()
{
  TangoHandler* tangoHandler = TangoHandler::getInstance();
  mojom::VRPassThroughCameraPtr seeThroughCameraPtr = nullptr;
  if (tangoHandler->isConnected())
  {
    seeThroughCameraPtr = mojom::VRPassThroughCamera::New();
    tangoHandler->getCameraImageSize(&(seeThroughCameraPtr->width), &(seeThroughCameraPtr->height));
    tangoHandler->getCameraImageTextureSize(&(seeThroughCameraPtr->textureWidth), &(seeThroughCameraPtr->textureHeight));
    tangoHandler->getCameraFocalLength(&(seeThroughCameraPtr->focalLengthX), &(seeThroughCameraPtr->focalLengthY));
    tangoHandler->getCameraPoint(&(seeThroughCameraPtr->pointX), &(seeThroughCameraPtr->pointY));
    seeThroughCameraPtr->orientation = tangoHandler->getSensorOrientation();
  }
  return seeThroughCameraPtr;
}

std::vector<mojom::VRHitPtr> TangoVRDevice::HitTest(float x, float y)
{
  std::vector<mojom::VRHitPtr> mojomHits;
  if (TangoHandler::getInstance()->isConnected())
  {
    std::vector<Hit> hits;
    if (TangoHandler::getInstance()->hitTest(x, y, hits) && hits.size() > 0)
    {
      std::vector<Hit>::size_type size = hits.size();
      mojomHits.resize(size);
      for (std::vector<Hit>::size_type i = 0; i < size; i++)
      {
        mojomHits[i] = mojom::VRHit::New();
        mojomHits[i]->modelMatrix.resize(16);
        for (int j = 0; j < 16; j++)
        {
          mojomHits[i]->modelMatrix[j] = hits[i].modelMatrix[j];
        }
      }
    }
  }
  return mojomHits;
}

std::vector<mojom::VRPlanePtr> TangoVRDevice::GetPlanes() {
  std::vector<mojom::VRPlanePtr> mojomPlanes;
  if (TangoHandler::getInstance()->isConnected())
  {
    std::vector<Plane> planes;
    if (TangoHandler::getInstance()->getPlanes(planes) && planes.size() > 0)
    {
      std::vector<Plane>::size_type size = planes.size();
      mojomPlanes.resize(size);
      for (std::vector<Plane>::size_type i = 0; i < size; i++)
      {
        mojomPlanes[i] = mojom::VRPlane::New();

        mojomPlanes[i]->identifier = planes[i].identifier;

        mojomPlanes[i]->modelMatrix.resize(16);
        for (int j = 0; j < 16; j++)
        {
          mojomPlanes[i]->modelMatrix[j] = planes[i].modelMatrix[j];
        }

        mojomPlanes[i]->extent.resize(2);
        for (int j = 0; j < 2; j++) {
          mojomPlanes[i]->extent[j] = planes[i].extent[j];
        }

        mojomPlanes[i]->vertices.resize(planes[i].count * 3);
        for (uint j = 0; j < planes[i].count * 3; j++) {
          mojomPlanes[i]->vertices[j] = planes[i].vertices[j];
        }
        mojomPlanes[i]->count = planes[i].count;
      }
    }
  }
  return mojomPlanes;
}

void TangoVRDevice::RequestPresent(const base::Callback<void(bool)>& callback) {
  // gvr_provider_->RequestPresent(callback);
}

void TangoVRDevice::SetSecureOrigin(bool secure_origin) {
  // secure_origin_ = secure_origin;
  // if (delegate_)
  //   delegate_->SetWebVRSecureOrigin(secure_origin_);
}

void TangoVRDevice::ExitPresent() {
  // gvr_provider_->ExitPresent();
  // OnExitPresent();
}

void TangoVRDevice::SubmitFrame(mojom::VRPosePtr pose) {
  // if (delegate_)
  //   delegate_->SubmitWebVRFrame();
}

void TangoVRDevice::UpdateLayerBounds(mojom::VRLayerBoundsPtr left_bounds,
                                  mojom::VRLayerBoundsPtr right_bounds) {
  // if (!delegate_)
  //   return;

  // gvr::Rectf left_gvr_bounds;
  // left_gvr_bounds.left = left_bounds->left;
  // left_gvr_bounds.top = 1.0f - left_bounds->top;
  // left_gvr_bounds.right = left_bounds->left + left_bounds->width;
  // left_gvr_bounds.bottom = 1.0f - (left_bounds->top + left_bounds->height);

  // gvr::Rectf right_gvr_bounds;
  // right_gvr_bounds.left = right_bounds->left;
  // right_gvr_bounds.top = 1.0f - right_bounds->top;
  // right_gvr_bounds.right = right_bounds->left + right_bounds->width;
  // right_gvr_bounds.bottom = 1.0f - (right_bounds->top + right_bounds->height);

  // delegate_->UpdateWebVRTextureBounds(left_gvr_bounds, right_gvr_bounds);
}

}  // namespace device
