// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/vr/VRPlane.h"

namespace blink {

VRPlane::VRPlane()
{
    m_modelMatrix = DOMFloat32Array::create(16);
    m_extent = DOMFloat32Array::create(2);
}

void VRPlane::setPlane(const device::mojom::blink::VRPlanePtr& planePtr)
{
    if (planePtr.is_null()) {
        return;
    }

    m_identifier = planePtr->identifier;

    for (size_t i = 0; i < 16; i++) {
    	m_modelMatrix->data()[i] = (float)planePtr->modelMatrix[i];
    }

    for (size_t i = 0; i < 2; i++) {
        m_extent->data()[i] = (float)planePtr->extent[i];
    }

    m_vertices = DOMFloat32Array::create(planePtr->count * 3);
    for (size_t i = 0; i < planePtr->count * 3; i++) {
    	m_vertices->data()[i] = (float)planePtr->vertices[i];
    }
}

DEFINE_TRACE(VRPlane)
{
    visitor->trace(m_modelMatrix);
    visitor->trace(m_extent);
    visitor->trace(m_vertices);
}

} // namespace blink
