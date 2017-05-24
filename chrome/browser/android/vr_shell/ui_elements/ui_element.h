// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_UI_ELEMENT_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_UI_ELEMENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "chrome/browser/android/vr_shell/ui_elements/ui_element_debug_id.h"
#include "device/vr/vr_types.h"

namespace base {
class TimeTicks;
}

namespace vr_shell {

class Animation;
class VrShellRenderer;

enum XAnchoring {
  XNONE = 0,
  XLEFT,
  XRIGHT,
};

enum YAnchoring {
  YNONE = 0,
  YTOP,
  YBOTTOM,
};

enum Fill {
  NONE = 0,
  // The element is filled with the content web site. Only one content element
  // may be added to the
  // scene.
  CONTENT = 1,
  // The element is filled with a radial gradient as specified by the edge and
  // center color.
  OPAQUE_GRADIENT = 2,
  // Same as OPAQUE_GRADIENT but the element is drawn as a grid.
  GRID_GRADIENT = 3,
  // The element draws itself.
  SELF = 4,
};

// TODO(vollick): use gfx::Transform crbug.com/718004
struct Transform {
  Transform();
  explicit Transform(const Transform& other);

  void MakeIdentity();
  void Rotate(const vr::Quatf& quat);
  void Rotate(const vr::RotationAxisAngle& axis_angle);
  void Translate(const gfx::Vector3dF& translation);
  void Scale(const gfx::Vector3dF& scale);

  vr::Mat4f to_world;
};

class WorldRectangle {
 public:
  const vr::Mat4f& TransformMatrix() const;
  Transform* mutable_transform() { return &transform_; }

  gfx::Point3F GetCenter() const;
  gfx::Vector3dF GetNormal() const;

  // Computes the distance from |ray_origin| to this rectangles's plane, along
  // |ray_vector|. Returns true and populates |distance| if the calculation is
  // possible, and false if the ray is parallel to the plane.
  bool GetRayDistance(const gfx::Point3F& ray_origin,
                      const gfx::Vector3dF& ray_vector,
                      float* distance) const;

  // Projects a 3D world point onto the X and Y axes of the transformed
  // rectangle, returning 2D coordinates relative to the un-transformed unit
  // rectangle. This allows beam intersection points to be mapped to sprite
  // pixel coordinates. Points that fall onto the rectangle will generate X and
  // Y values on the interval [-0.5, 0.5].
  gfx::PointF GetUnitRectangleCoordinates(
      const gfx::Point3F& world_point) const;

 private:
  Transform transform_;
};

class UiElement : public WorldRectangle {
 public:
  UiElement();
  virtual ~UiElement();

  virtual void OnBeginFrame(const base::TimeTicks& begin_frame_time);

  void Animate(const base::TimeTicks& time);

  // Indicates whether the element should be visually rendered.
  bool IsVisible() const;

  // Indicates whether the element should be tested for cursor input.
  bool IsHitTestable() const;

  virtual void Render(VrShellRenderer* renderer,
                      vr::Mat4f view_proj_matrix) const;

  virtual void Initialize();

  // Controller interaction methods.
  virtual void OnHoverEnter(const gfx::PointF& position);
  virtual void OnHoverLeave();
  virtual void OnMove(const gfx::PointF& position);
  virtual void OnButtonDown(const gfx::PointF& position);
  virtual void OnButtonUp(const gfx::PointF& position);
  // Whether the point (relative to the origin of the element), should be
  // considered on the element. All elements are considered rectangular by
  // default though elements may override this function to handle arbitrary
  // shapes. Points within the rectangular area are mapped from 0:1 as follows,
  // though will extend outside this range when outside of the element:
  // [(0.0, 0.0), (1.0, 0.0)
  //  (1.0, 0.0), (1.0, 1.0)]
  virtual bool HitTest(const gfx::PointF& point) const;

  int id() const { return id_; }
  void set_id(int id) { id_ = id; }

  // If a non-negative parent ID is specified, applicable transformations
  // are applied relative to the parent, rather than absolutely.
  int parent_id() const { return parent_id_; }
  void set_parent_id(int id) { parent_id_ = id; }

  // If true, this object will be visible.
  bool visible() const { return visible_; }
  void set_visible(bool visible) { visible_ = visible; }

  // If false, the reticle will not hit the element, even if visible.
  bool hit_testable() const { return hit_testable_; }
  void set_hit_testable(bool hit_testable) { hit_testable_ = hit_testable; }

  // If true, transformations will be applied relative to the field of view,
  // rather than the world.
  bool lock_to_fov() const { return lock_to_fov_; }
  void set_lock_to_fov(bool lock) { lock_to_fov_ = lock; }

  // If true should be drawn in the world viewport, but over all other elements.
  bool is_overlay() const { return is_overlay_; }
  void set_is_overlay(bool is_overlay) { is_overlay_ = is_overlay; }

  // The computed lock to the FoV, incorporating lock of parent objects.
  bool computed_lock_to_fov() const { return computed_lock_to_fov_; }
  void set_computed_lock_to_fov(bool computed_lock) {
    computed_lock_to_fov_ = computed_lock;
  }

  // The size of the object.  This does not affect children.
  gfx::Vector3dF size() const { return size_; }
  void set_size(const gfx::Vector3dF& size) { size_ = size; }

  // The scale of the object, and its children.
  gfx::Vector3dF scale() const { return scale_; }
  void set_scale(const gfx::Vector3dF& scale) { scale_ = scale; }

  // The rotation of the object, and its children.
  vr::RotationAxisAngle rotation() const { return rotation_; }
  void set_rotation(const vr::RotationAxisAngle& rotation) {
    rotation_ = rotation;
  }

  // The translation of the object, and its children.  Translation is applied
  // after rotation and scaling.
  gfx::Vector3dF translation() const { return translation_; }
  void set_translation(const gfx::Vector3dF& translation) {
    translation_ = translation;
  }

  // The opacity of the object (between 0.0 and 1.0).
  float opacity() const { return opacity_; }
  void set_opacity(float opacity) { opacity_ = opacity; }

  // The computed opacity, incorporating opacity of parent objects.
  float computed_opacity() const { return computed_opacity_; }
  void set_computed_opacity(float computed_opacity) {
    computed_opacity_ = computed_opacity;
  }

  // If anchoring is specified, the translation will be relative to the
  // specified edge(s) of the parent, rather than the center.  A parent object
  // must be specified when using anchoring.
  XAnchoring x_anchoring() const { return x_anchoring_; }
  void set_x_anchoring(XAnchoring x_anchoring) { x_anchoring_ = x_anchoring; }

  YAnchoring y_anchoring() const { return y_anchoring_; }
  void set_y_anchoring(YAnchoring y_anchoring) { y_anchoring_ = y_anchoring; }

  // Animations that affect the properties of the object over time.
  std::vector<std::unique_ptr<Animation>>& animations() { return animations_; }
  const std::vector<std::unique_ptr<Animation>>& animations() const {
    return animations_;
  }

  Fill fill() const { return fill_; }
  void set_fill(Fill fill) { fill_ = fill; }

  vr::Colorf edge_color() const { return edge_color_; }
  void set_edge_color(const vr::Colorf& edge_color) {
    edge_color_ = edge_color;
  }

  vr::Colorf center_color() const { return center_color_; }
  void set_center_color(const vr::Colorf& center_color) {
    center_color_ = center_color;
  }

  int gridline_count() const { return gridline_count_; }
  void set_gridline_count(int gridline_count) {
    gridline_count_ = gridline_count;
  }

  int draw_phase() const { return draw_phase_; }
  void set_draw_phase(int draw_phase) { draw_phase_ = draw_phase; }

  // This transform can be used by children to derive position of its parent.
  Transform& inheritable_transform() { return inheritable_transform_; }
  const Transform& inheritable_transform() const {
    return inheritable_transform_;
  }
  void set_inheritable_transform(const Transform& transform) {
    inheritable_transform_ = transform;
  }

  // A flag usable during transformation calculates to avoid duplicate work.
  bool dirty() const { return dirty_; }
  void set_dirty(bool dirty) { dirty_ = dirty; }

  // A flag usable during transformation calculates to avoid duplicate work.
  UiElementDebugId debug_id() const { return debug_id_; }
  void set_debug_id(UiElementDebugId debug_id) { debug_id_ = debug_id; }

  // By default, sets an element to be visible or not. This may be overridden to
  // allow finer control of element visibility.
  virtual void SetEnabled(bool enabled);

 private:
  // Valid IDs are non-negative.
  int id_ = -1;

  // If a non-negative parent ID is specified, applicable transformations
  // are applied relative to the parent, rather than absolutely.
  int parent_id_ = -1;

  // If true, this object will be visible.
  bool visible_ = false;

  // If false, the reticle will not hit the element, even if visible.
  bool hit_testable_ = true;

  // If true, transformations will be applied relative to the field of view,
  // rather than the world.
  bool lock_to_fov_ = false;

  // The computed lock to the FoV, incorporating lock of parent objects.
  bool computed_lock_to_fov_ = false;

  // If true, then this element will be drawn in the world viewport, but above
  // all other elements.
  bool is_overlay_ = false;

  // The size of the object.  This does not affect children.
  gfx::Vector3dF size_ = {1.0f, 1.0f, 1.0f};

  // The scale of the object, and its children.
  gfx::Vector3dF scale_ = {1.0f, 1.0f, 1.0f};

  // The rotation of the object, and its children.
  vr::RotationAxisAngle rotation_ = {1.0f, 0.0f, 0.0f, 0.0f};

  // The translation of the object, and its children.  Translation is applied
  // after rotation and scaling.
  gfx::Vector3dF translation_ = {0.0f, 0.0f, 0.0f};

  // The opacity of the object (between 0.0 and 1.0).
  float opacity_ = 1.0f;

  // The computed opacity, incorporating opacity of parent objects.
  float computed_opacity_ = 1.0f;

  // If anchoring is specified, the translation will be relative to the
  // specified edge(s) of the parent, rather than the center.  A parent object
  // must be specified when using anchoring.
  XAnchoring x_anchoring_ = XAnchoring::XNONE;
  YAnchoring y_anchoring_ = YAnchoring::YNONE;

  // Animations that affect the properties of the object over time.
  std::vector<std::unique_ptr<Animation>> animations_;

  Fill fill_ = Fill::NONE;

  vr::Colorf edge_color_ = {1.0f, 1.0f, 1.0f, 1.0f};
  vr::Colorf center_color_ = {1.0f, 1.0f, 1.0f, 1.0f};

  int gridline_count_ = 1;

  int draw_phase_ = 1;

  // This transform can be used by children to derive position of its parent.
  Transform inheritable_transform_;

  // A flag usable during transformation calculates to avoid duplicate work.
  bool dirty_ = false;

  // An identifier used for testing and debugging, in lieu of a string.
  UiElementDebugId debug_id_ = UiElementDebugId::kNone;

  Transform transform_;

  DISALLOW_COPY_AND_ASSIGN(UiElement);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_UI_ELEMENTS_UI_ELEMENT_H_
