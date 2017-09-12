// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VRPlane_h
#define VRPlane_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "core/dom/DOMTypedArray.h"
#include "device/vr/vr_service.mojom-blink.h"
#include "platform/heap/Handle.h"
#include "wtf/Forward.h"

namespace blink {

class VRPlane final : public GarbageCollected<VRPlane>, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    VRPlane();

    long identifier() const { return m_identifier; }
    DOMFloat32Array* modelMatrix() const { return m_modelMatrix; }
    DOMFloat32Array* extent() const { return m_extent; }
    DOMFloat32Array* vertices() const { return m_vertices; }

    void setPlane(const device::mojom::blink::VRPlanePtr&);

    DECLARE_VIRTUAL_TRACE();

private:
    long m_identifier;
    Member<DOMFloat32Array> m_modelMatrix;
    Member<DOMFloat32Array> m_extent;
    Member<DOMFloat32Array> m_vertices;
};

} // namespace blink

#endif // VRPlane_h
