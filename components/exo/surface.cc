// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/surface.h"

#include <utility>

#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/output/compositor_frame_sink.h"
#include "cc/quads/render_pass.h"
#include "cc/quads/shared_quad_state.h"
#include "cc/quads/solid_color_draw_quad.h"
#include "cc/quads/texture_draw_quad.h"
#include "cc/resources/single_release_callback.h"
#include "cc/surfaces/sequence_surface_reference_factory.h"
#include "cc/surfaces/surface.h"
#include "cc/surfaces/surface_manager.h"
#include "components/exo/buffer.h"
#include "components/exo/pointer.h"
#include "components/exo/surface_delegate.h"
#include "components/exo/surface_observer.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "ui/aura/env.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_targeter.h"
#include "ui/base/class_property.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/geometry/safe_integer_conversions.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/path.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/transform_util.h"
#include "ui/views/widget/widget.h"

DECLARE_UI_CLASS_PROPERTY_TYPE(exo::Surface*);

namespace exo {
namespace {

// A property key containing the surface that is associated with
// window. If unset, no surface is associated with window.
DEFINE_UI_CLASS_PROPERTY_KEY(Surface*, kSurfaceKey, nullptr);

// Helper function that returns an iterator to the first entry in |list|
// with |key|.
template <typename T, typename U>
typename T::iterator FindListEntry(T& list, U key) {
  return std::find_if(list.begin(), list.end(),
                      [key](const typename T::value_type& entry) {
                        return entry.first == key;
                      });
}

// Helper function that returns true if |list| contains an entry with |key|.
template <typename T, typename U>
bool ListContainsEntry(T& list, U key) {
  return FindListEntry(list, key) != list.end();
}

// Helper function that returns true if |format| may have an alpha channel.
// Note: False positives are allowed but false negatives are not.
bool FormatHasAlpha(gfx::BufferFormat format) {
  switch (format) {
    case gfx::BufferFormat::BGR_565:
    case gfx::BufferFormat::RGBX_8888:
    case gfx::BufferFormat::BGRX_8888:
    case gfx::BufferFormat::YVU_420:
    case gfx::BufferFormat::YUV_420_BIPLANAR:
    case gfx::BufferFormat::UYVY_422:
      return false;
    default:
      return true;
  }
}

class CustomWindowDelegate : public aura::WindowDelegate {
 public:
  explicit CustomWindowDelegate(Surface* surface) : surface_(surface) {}
  ~CustomWindowDelegate() override {}

  // Overridden from aura::WindowDelegate:
  gfx::Size GetMinimumSize() const override { return gfx::Size(); }
  gfx::Size GetMaximumSize() const override { return gfx::Size(); }
  void OnBoundsChanged(const gfx::Rect& old_bounds,
                       const gfx::Rect& new_bounds) override {}
  gfx::NativeCursor GetCursor(const gfx::Point& point) override {
    return surface_->GetCursor();
  }
  int GetNonClientComponent(const gfx::Point& point) const override {
    return HTNOWHERE;
  }
  bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) override {
    return true;
  }
  bool CanFocus() override { return true; }
  void OnCaptureLost() override {}
  void OnPaint(const ui::PaintContext& context) override {}
  void OnDeviceScaleFactorChanged(float device_scale_factor) override {
    surface_->SetDeviceScaleFactor(device_scale_factor);
  }
  void OnWindowDestroying(aura::Window* window) override {}
  void OnWindowDestroyed(aura::Window* window) override { delete this; }
  void OnWindowTargetVisibilityChanged(bool visible) override {}
  bool HasHitTestMask() const override { return surface_->HasHitTestMask(); }
  void GetHitTestMask(gfx::Path* mask) const override {
    surface_->GetHitTestMask(mask);
  }
  void OnKeyEvent(ui::KeyEvent* event) override {
    // Propagates the key event upto the top-level views Widget so that we can
    // trigger proper events in the views/ash level there. Event handling for
    // Surfaces is done in a post event handler in keyboard.cc.
    views::Widget* widget =
        views::Widget::GetTopLevelWidgetForNativeView(surface_->window());
    if (widget)
      widget->OnKeyEvent(event);
  }

 private:
  Surface* const surface_;

  DISALLOW_COPY_AND_ASSIGN(CustomWindowDelegate);
};

class CustomWindowTargeter : public aura::WindowTargeter {
 public:
  CustomWindowTargeter() {}
  ~CustomWindowTargeter() override {}

  // Overridden from aura::WindowTargeter:
  bool EventLocationInsideBounds(aura::Window* window,
                                 const ui::LocatedEvent& event) const override {
    Surface* surface = Surface::AsSurface(window);
    if (!surface)
      return false;

    gfx::Point local_point = event.location();
    if (window->parent())
      aura::Window::ConvertPointToTarget(window->parent(), window,
                                         &local_point);
    return surface->HitTestRect(gfx::Rect(local_point, gfx::Size(1, 1)));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CustomWindowTargeter);
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Surface, public:

// TODO(fsamuel): exo should not use context_factory_private. Instead, we should
// request a CompositorFrameSink from the aura::Window. Setting up the
// BeginFrame hierarchy should be an internal implementation detail of aura or
// mus in aura-mus.
Surface::Surface() : window_(new aura::Window(new CustomWindowDelegate(this))) {
  window_->SetType(ui::wm::WINDOW_TYPE_CONTROL);
  window_->SetName("ExoSurface");
  window_->SetProperty(kSurfaceKey, this);
  window_->Init(ui::LAYER_SOLID_COLOR);
  window_->SetEventTargeter(base::WrapUnique(new CustomWindowTargeter));
  window_->set_owned_by_parent(false);
  window_->AddObserver(this);
  aura::Env::GetInstance()->context_factory()->AddObserver(this);
  compositor_frame_sink_holder_ = base::MakeUnique<CompositorFrameSinkHolder>(
      this, window_->CreateCompositorFrameSink());
}

Surface::~Surface() {
  aura::Env::GetInstance()->context_factory()->RemoveObserver(this);
  for (SurfaceObserver& observer : observers_)
    observer.OnSurfaceDestroying(this);

  window_->RemoveObserver(this);
  if (window_->layer()->GetCompositor())
    window_->layer()->GetCompositor()->vsync_manager()->RemoveObserver(this);
  window_->layer()->SetShowSolidColorContent();

  frame_callbacks_.splice(frame_callbacks_.end(), pending_frame_callbacks_);
  active_frame_callbacks_.splice(active_frame_callbacks_.end(),
                                 frame_callbacks_);
  // Call all frame callbacks with a null frame time to indicate that they
  // have been cancelled.
  for (const auto& frame_callback : active_frame_callbacks_)
    frame_callback.Run(base::TimeTicks());

  presentation_callbacks_.splice(presentation_callbacks_.end(),
                                 pending_presentation_callbacks_);
  swapping_presentation_callbacks_.splice(
      swapping_presentation_callbacks_.end(), presentation_callbacks_);
  swapped_presentation_callbacks_.splice(swapped_presentation_callbacks_.end(),
                                         swapping_presentation_callbacks_);
  // Call all presentation callbacks with a null presentation time to indicate
  // that they have been cancelled.
  for (const auto& presentation_callback : swapped_presentation_callbacks_)
    presentation_callback.Run(base::TimeTicks(), base::TimeDelta());
}

// static
Surface* Surface::AsSurface(const aura::Window* window) {
  return window->GetProperty(kSurfaceKey);
}

cc::SurfaceId Surface::GetSurfaceId() const {
  return window_->GetSurfaceId();
}

void Surface::Attach(Buffer* buffer) {
  TRACE_EVENT1("exo", "Surface::Attach", "buffer",
               buffer ? buffer->GetSize().ToString() : "null");

  has_pending_contents_ = true;
  pending_buffer_.Reset(buffer ? buffer->AsWeakPtr() : base::WeakPtr<Buffer>());
}

void Surface::Damage(const gfx::Rect& damage) {
  TRACE_EVENT1("exo", "Surface::Damage", "damage", damage.ToString());

  pending_damage_.op(gfx::RectToSkIRect(damage), SkRegion::kUnion_Op);
}

void Surface::RequestFrameCallback(const FrameCallback& callback) {
  TRACE_EVENT0("exo", "Surface::RequestFrameCallback");

  pending_frame_callbacks_.push_back(callback);
}

void Surface::RequestPresentationCallback(
    const PresentationCallback& callback) {
  TRACE_EVENT0("exo", "Surface::RequestPresentationCallback");

  pending_presentation_callbacks_.push_back(callback);
}

void Surface::SetOpaqueRegion(const SkRegion& region) {
  TRACE_EVENT1("exo", "Surface::SetOpaqueRegion", "region",
               gfx::SkIRectToRect(region.getBounds()).ToString());

  pending_state_.opaque_region = region;
}

void Surface::SetInputRegion(const SkRegion& region) {
  TRACE_EVENT1("exo", "Surface::SetInputRegion", "region",
               gfx::SkIRectToRect(region.getBounds()).ToString());

  pending_state_.input_region = region;
}

void Surface::SetBufferScale(float scale) {
  TRACE_EVENT1("exo", "Surface::SetBufferScale", "scale", scale);

  pending_state_.buffer_scale = scale;
}

void Surface::AddSubSurface(Surface* sub_surface) {
  TRACE_EVENT1("exo", "Surface::AddSubSurface", "sub_surface",
               sub_surface->AsTracedValue());

  DCHECK(!sub_surface->window()->parent());
  DCHECK(!sub_surface->window()->IsVisible());
  window_->AddChild(sub_surface->window());

  DCHECK(!ListContainsEntry(pending_sub_surfaces_, sub_surface));
  pending_sub_surfaces_.push_back(std::make_pair(sub_surface, gfx::Point()));
  has_pending_layer_changes_ = true;
}

void Surface::RemoveSubSurface(Surface* sub_surface) {
  TRACE_EVENT1("exo", "Surface::RemoveSubSurface", "sub_surface",
               sub_surface->AsTracedValue());

  window_->RemoveChild(sub_surface->window());
  if (sub_surface->window()->IsVisible())
    sub_surface->window()->Hide();

  DCHECK(ListContainsEntry(pending_sub_surfaces_, sub_surface));
  pending_sub_surfaces_.erase(
      FindListEntry(pending_sub_surfaces_, sub_surface));
  has_pending_layer_changes_ = true;
}

void Surface::SetSubSurfacePosition(Surface* sub_surface,
                                    const gfx::Point& position) {
  TRACE_EVENT2("exo", "Surface::SetSubSurfacePosition", "sub_surface",
               sub_surface->AsTracedValue(), "position", position.ToString());

  auto it = FindListEntry(pending_sub_surfaces_, sub_surface);
  DCHECK(it != pending_sub_surfaces_.end());
  if (it->second == position)
    return;
  it->second = position;
  has_pending_layer_changes_ = true;
}

void Surface::PlaceSubSurfaceAbove(Surface* sub_surface, Surface* reference) {
  TRACE_EVENT2("exo", "Surface::PlaceSubSurfaceAbove", "sub_surface",
               sub_surface->AsTracedValue(), "reference",
               reference->AsTracedValue());

  if (sub_surface == reference) {
    DLOG(WARNING) << "Client tried to place sub-surface above itself";
    return;
  }

  auto position_it = pending_sub_surfaces_.begin();
  if (reference != this) {
    position_it = FindListEntry(pending_sub_surfaces_, reference);
    if (position_it == pending_sub_surfaces_.end()) {
      DLOG(WARNING) << "Client tried to place sub-surface above a reference "
                       "surface that is neither a parent nor a sibling";
      return;
    }

    // Advance iterator to have |position_it| point to the sibling surface
    // above |reference|.
    ++position_it;
  }

  DCHECK(ListContainsEntry(pending_sub_surfaces_, sub_surface));
  auto it = FindListEntry(pending_sub_surfaces_, sub_surface);
  if (it == position_it)
    return;
  pending_sub_surfaces_.splice(position_it, pending_sub_surfaces_, it);
  has_pending_layer_changes_ = true;
}

void Surface::PlaceSubSurfaceBelow(Surface* sub_surface, Surface* sibling) {
  TRACE_EVENT2("exo", "Surface::PlaceSubSurfaceBelow", "sub_surface",
               sub_surface->AsTracedValue(), "sibling",
               sibling->AsTracedValue());

  if (sub_surface == sibling) {
    DLOG(WARNING) << "Client tried to place sub-surface below itself";
    return;
  }

  auto sibling_it = FindListEntry(pending_sub_surfaces_, sibling);
  if (sibling_it == pending_sub_surfaces_.end()) {
    DLOG(WARNING) << "Client tried to place sub-surface below a surface that "
                     "is not a sibling";
    return;
  }

  DCHECK(ListContainsEntry(pending_sub_surfaces_, sub_surface));
  auto it = FindListEntry(pending_sub_surfaces_, sub_surface);
  if (it == sibling_it)
    return;
  pending_sub_surfaces_.splice(sibling_it, pending_sub_surfaces_, it);
  has_pending_layer_changes_ = true;
}

void Surface::SetViewport(const gfx::Size& viewport) {
  TRACE_EVENT1("exo", "Surface::SetViewport", "viewport", viewport.ToString());

  pending_state_.viewport = viewport;
}

void Surface::SetCrop(const gfx::RectF& crop) {
  TRACE_EVENT1("exo", "Surface::SetCrop", "crop", crop.ToString());

  pending_state_.crop = crop;
}

void Surface::SetOnlyVisibleOnSecureOutput(bool only_visible_on_secure_output) {
  TRACE_EVENT1("exo", "Surface::SetOnlyVisibleOnSecureOutput",
               "only_visible_on_secure_output", only_visible_on_secure_output);

  pending_state_.only_visible_on_secure_output = only_visible_on_secure_output;
}

void Surface::SetBlendMode(SkBlendMode blend_mode) {
  TRACE_EVENT1("exo", "Surface::SetBlendMode", "blend_mode",
               static_cast<int>(blend_mode));

  pending_state_.blend_mode = blend_mode;
}

void Surface::SetAlpha(float alpha) {
  TRACE_EVENT1("exo", "Surface::SetAlpha", "alpha", alpha);

  pending_state_.alpha = alpha;
}

void Surface::SetDeviceScaleFactor(float device_scale_factor) {
  device_scale_factor_ = device_scale_factor;
}

void Surface::Commit() {
  TRACE_EVENT0("exo", "Surface::Commit");

  needs_commit_surface_hierarchy_ = true;

  if (state_ != pending_state_)
    has_pending_layer_changes_ = true;

  if (has_pending_contents_) {
    if (pending_buffer_.buffer()) {
      if (current_resource_.size != pending_buffer_.buffer()->GetSize())
        has_pending_layer_changes_ = true;
      // Whether layer fills bounds opaquely or not might have changed.
      if (current_resource_has_alpha_ !=
          FormatHasAlpha(pending_buffer_.buffer()->GetFormat()))
        has_pending_layer_changes_ = true;
    } else if (!current_resource_.size.IsEmpty()) {
      has_pending_layer_changes_ = true;
    }
  }

  if (delegate_) {
    delegate_->OnSurfaceCommit();
  } else {
    CheckIfSurfaceHierarchyNeedsCommitToNewSurfaces();
    CommitSurfaceHierarchy();
  }

  if (current_begin_frame_ack_.sequence_number !=
      cc::BeginFrameArgs::kInvalidFrameNumber) {
    if (begin_frame_source_)
      begin_frame_source_->DidFinishFrame(this, current_begin_frame_ack_);
    if (!current_begin_frame_ack_.has_damage) {
      compositor_frame_sink_holder_->GetCompositorFrameSink()
          ->DidNotProduceFrame(current_begin_frame_ack_);
    }
    current_begin_frame_ack_.sequence_number =
        cc::BeginFrameArgs::kInvalidFrameNumber;
  }
}

void Surface::CommitSurfaceHierarchy() {
  DCHECK(needs_commit_surface_hierarchy_);
  needs_commit_surface_hierarchy_ = false;
  has_pending_layer_changes_ = false;

  state_ = pending_state_;
  pending_state_.only_visible_on_secure_output = false;

  // We update contents if Attach() has been called since last commit.
  if (has_pending_contents_) {
    has_pending_contents_ = false;

    current_buffer_ = std::move(pending_buffer_);

    UpdateResource(true);
  }

  // Move pending frame callbacks to the end of frame_callbacks_.
  frame_callbacks_.splice(frame_callbacks_.end(), pending_frame_callbacks_);

  // Move pending presentation callbacks to the end of presentation_callbacks_.
  presentation_callbacks_.splice(presentation_callbacks_.end(),
                                 pending_presentation_callbacks_);

  UpdateSurface(false);

  if (needs_commit_to_new_surface_) {
    needs_commit_to_new_surface_ = false;
    window_->layer()->SetFillsBoundsOpaquely(
        !current_resource_has_alpha_ ||
        state_.blend_mode == SkBlendMode::kSrc ||
        state_.opaque_region.contains(
            gfx::RectToSkIRect(gfx::Rect(content_size_))));
  }

  // Reset damage.
  pending_damage_.setEmpty();
  DCHECK(!current_resource_.id ||
         compositor_frame_sink_holder_->HasReleaseCallbackForResource(
             current_resource_.id));

  // Synchronize window hierarchy. This will position and update the stacking
  // order of all sub-surfaces after committing all pending state of sub-surface
  // descendants.
  aura::Window* stacking_target = nullptr;
  for (auto& sub_surface_entry : pending_sub_surfaces_) {
    Surface* sub_surface = sub_surface_entry.first;

    // Synchronsouly commit all pending state of the sub-surface and its
    // decendents.
    if (sub_surface->needs_commit_surface_hierarchy())
      sub_surface->CommitSurfaceHierarchy();

    // Enable/disable sub-surface based on if it has contents.
    if (sub_surface->has_contents())
      sub_surface->window()->Show();
    else
      sub_surface->window()->Hide();

    // Move sub-surface to its new position in the stack.
    if (stacking_target)
      window_->StackChildAbove(sub_surface->window(), stacking_target);

    // Stack next sub-surface above this sub-surface.
    stacking_target = sub_surface->window();

    // Update sub-surface position relative to surface origin.
    sub_surface->window()->SetBounds(gfx::Rect(
        sub_surface_entry.second, sub_surface->window()->layer()->size()));
  }
}

bool Surface::IsSynchronized() const {
  return delegate_ ? delegate_->IsSurfaceSynchronized() : false;
}

gfx::Rect Surface::GetHitTestBounds() const {
  SkIRect bounds = state_.input_region.getBounds();
  if (!bounds.intersect(
          gfx::RectToSkIRect(gfx::Rect(window_->layer()->size()))))
    return gfx::Rect();
  return gfx::SkIRectToRect(bounds);
}

bool Surface::HitTestRect(const gfx::Rect& rect) const {
  if (HasHitTestMask())
    return state_.input_region.intersects(gfx::RectToSkIRect(rect));

  return rect.Intersects(gfx::Rect(window_->layer()->size()));
}

bool Surface::HasHitTestMask() const {
  return !state_.input_region.contains(
      gfx::RectToSkIRect(gfx::Rect(window_->layer()->size())));
}

void Surface::GetHitTestMask(gfx::Path* mask) const {
  state_.input_region.getBoundaryPath(mask);
}

void Surface::RegisterCursorProvider(Pointer* provider) {
  cursor_providers_.insert(provider);
}

void Surface::UnregisterCursorProvider(Pointer* provider) {
  cursor_providers_.erase(provider);
}

gfx::NativeCursor Surface::GetCursor() {
  // What cursor we display when we have multiple cursor providers is not
  // important. Return the first non-null cursor.
  for (auto* cursor_provider : cursor_providers_) {
    gfx::NativeCursor cursor = cursor_provider->GetCursor();
    if (cursor != ui::CursorType::kNull)
      return cursor;
  }
  return ui::CursorType::kNull;
}

void Surface::SetSurfaceDelegate(SurfaceDelegate* delegate) {
  DCHECK(!delegate_ || !delegate);
  delegate_ = delegate;
}

bool Surface::HasSurfaceDelegate() const {
  return !!delegate_;
}

void Surface::AddSurfaceObserver(SurfaceObserver* observer) {
  observers_.AddObserver(observer);
}

void Surface::RemoveSurfaceObserver(SurfaceObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool Surface::HasSurfaceObserver(const SurfaceObserver* observer) const {
  return observers_.HasObserver(observer);
}

std::unique_ptr<base::trace_event::TracedValue> Surface::AsTracedValue() const {
  std::unique_ptr<base::trace_event::TracedValue> value(
      new base::trace_event::TracedValue());
  value->SetString("name", window_->layer()->name());
  return value;
}

void Surface::DidReceiveCompositorFrameAck() {
  active_frame_callbacks_.splice(active_frame_callbacks_.end(),
                                 frame_callbacks_);
  swapping_presentation_callbacks_.splice(
      swapping_presentation_callbacks_.end(), presentation_callbacks_);
  UpdateNeedsBeginFrame();
}

void Surface::SetBeginFrameSource(cc::BeginFrameSource* begin_frame_source) {
  if (needs_begin_frame_) {
    DCHECK(begin_frame_source_);
    begin_frame_source_->RemoveObserver(this);
    needs_begin_frame_ = false;
  }
  begin_frame_source_ = begin_frame_source;
  UpdateNeedsBeginFrame();
}

void Surface::UpdateNeedsBeginFrame() {
  if (!begin_frame_source_)
    return;

  bool needs_begin_frame = !active_frame_callbacks_.empty();
  if (needs_begin_frame == needs_begin_frame_)
    return;

  needs_begin_frame_ = needs_begin_frame;
  if (needs_begin_frame_)
    begin_frame_source_->AddObserver(this);
  else
    begin_frame_source_->RemoveObserver(this);
}

bool Surface::OnBeginFrameDerivedImpl(const cc::BeginFrameArgs& args) {
  current_begin_frame_ack_ = cc::BeginFrameAck(
      args.source_id, args.sequence_number, args.sequence_number, false);
  while (!active_frame_callbacks_.empty()) {
    active_frame_callbacks_.front().Run(args.frame_time);
    active_frame_callbacks_.pop_front();
  }
  return true;
}

void Surface::CheckIfSurfaceHierarchyNeedsCommitToNewSurfaces() {
  if (HasLayerHierarchyChanged())
    SetSurfaceHierarchyNeedsCommitToNewSurfaces();
}

////////////////////////////////////////////////////////////////////////////////
// ui::ContextFactoryObserver overrides:

void Surface::OnLostResources() {
  if (!window_->GetSurfaceId().is_valid())
    return;
  UpdateResource(false);
  UpdateSurface(true);
}

////////////////////////////////////////////////////////////////////////////////
// aura::WindowObserver overrides:

void Surface::OnWindowAddedToRootWindow(aura::Window* window) {
  window->layer()->GetCompositor()->vsync_manager()->AddObserver(this);
}

void Surface::OnWindowRemovingFromRootWindow(aura::Window* window,
                                             aura::Window* new_root) {
  window->layer()->GetCompositor()->vsync_manager()->RemoveObserver(this);
}

////////////////////////////////////////////////////////////////////////////////
// ui::CompositorVSyncManager::Observer overrides:

void Surface::OnUpdateVSyncParameters(base::TimeTicks timebase,
                                      base::TimeDelta interval) {
  // Use current time if platform doesn't provide an accurate timebase.
  if (timebase.is_null())
    timebase = base::TimeTicks::Now();

  while (!swapped_presentation_callbacks_.empty()) {
    swapped_presentation_callbacks_.front().Run(timebase, interval);
    swapped_presentation_callbacks_.pop_front();
  }

  // VSync parameters updates are generated at the start of a new swap. Move
  // the swapping presentation callbacks to swapped callbacks so they fire
  // at the next VSync parameters update as that will contain the presentation
  // time for the previous frame.
  swapped_presentation_callbacks_.splice(swapped_presentation_callbacks_.end(),
                                         swapping_presentation_callbacks_);
}

////////////////////////////////////////////////////////////////////////////////
// Buffer, private:

Surface::State::State() : input_region(SkIRect::MakeLargest()) {}

Surface::State::~State() = default;

bool Surface::State::operator==(const State& other) {
  return (other.crop == crop && alpha == other.alpha &&
          other.blend_mode == blend_mode && other.viewport == viewport &&
          other.opaque_region == opaque_region &&
          other.buffer_scale == buffer_scale &&
          other.input_region == input_region);
}

Surface::BufferAttachment::BufferAttachment() {}

Surface::BufferAttachment::~BufferAttachment() {
  if (buffer_)
    buffer_->OnDetach();
}

Surface::BufferAttachment& Surface::BufferAttachment::operator=(
    BufferAttachment&& other) {
  if (buffer_)
    buffer_->OnDetach();
  buffer_ = other.buffer_;
  other.buffer_ = base::WeakPtr<Buffer>();
  return *this;
}

base::WeakPtr<Buffer>& Surface::BufferAttachment::buffer() {
  return buffer_;
}

const base::WeakPtr<Buffer>& Surface::BufferAttachment::buffer() const {
  return buffer_;
}

void Surface::BufferAttachment::Reset(base::WeakPtr<Buffer> buffer) {
  if (buffer)
    buffer->OnAttach();
  if (buffer_)
    buffer_->OnDetach();
  buffer_ = buffer;
}

bool Surface::HasLayerHierarchyChanged() const {
  if (needs_commit_surface_hierarchy_ && has_pending_layer_changes_)
    return true;

  for (const auto& sub_surface_entry : pending_sub_surfaces_) {
    if (sub_surface_entry.first->HasLayerHierarchyChanged())
      return true;
  }
  return false;
}

void Surface::SetSurfaceHierarchyNeedsCommitToNewSurfaces() {
  needs_commit_to_new_surface_ = true;
  for (auto& sub_surface_entry : pending_sub_surfaces_) {
    sub_surface_entry.first->SetSurfaceHierarchyNeedsCommitToNewSurfaces();
  }
}

void Surface::UpdateResource(bool client_usage) {
  if (current_buffer_.buffer() &&
      current_buffer_.buffer()->ProduceTransferableResource(
          compositor_frame_sink_holder_.get(), next_resource_id_++,
          state_.only_visible_on_secure_output, client_usage,
          &current_resource_)) {
    current_resource_has_alpha_ =
        FormatHasAlpha(current_buffer_.buffer()->GetFormat());
  } else {
    current_resource_.id = 0;
    current_resource_.size = gfx::Size();
    current_resource_has_alpha_ = false;
  }
}

void Surface::UpdateSurface(bool full_damage) {
  gfx::Size buffer_size = current_resource_.size;
  gfx::SizeF scaled_buffer_size(
      gfx::ScaleSize(gfx::SizeF(buffer_size), 1.0f / state_.buffer_scale));

  gfx::Size layer_size;  // Size of the output layer, in DIP.
  if (!state_.viewport.IsEmpty()) {
    layer_size = state_.viewport;
  } else if (!state_.crop.IsEmpty()) {
    DLOG_IF(WARNING, !gfx::IsExpressibleAsInt(state_.crop.width()) ||
                         !gfx::IsExpressibleAsInt(state_.crop.height()))
        << "Crop rectangle size (" << state_.crop.size().ToString()
        << ") most be expressible using integers when viewport is not set";
    layer_size = gfx::ToCeiledSize(state_.crop.size());
  } else {
    layer_size = gfx::ToCeiledSize(scaled_buffer_size);
  }

  content_size_ = layer_size;
  // TODO(jbauman): Figure out how this interacts with the pixel size of
  // CopyOutputRequests on the layer.
  gfx::Size contents_surface_size = layer_size;

  gfx::PointF uv_top_left(0.f, 0.f);
  gfx::PointF uv_bottom_right(1.f, 1.f);
  if (!state_.crop.IsEmpty()) {
    uv_top_left = state_.crop.origin();

    uv_top_left.Scale(1.f / scaled_buffer_size.width(),
                      1.f / scaled_buffer_size.height());
    uv_bottom_right = state_.crop.bottom_right();
    uv_bottom_right.Scale(1.f / scaled_buffer_size.width(),
                          1.f / scaled_buffer_size.height());
  }

  gfx::Rect damage_rect;
  gfx::Rect output_rect = gfx::Rect(contents_surface_size);
  if (full_damage) {
    damage_rect = output_rect;
  } else {
    // pending_damage_ is in Surface coordinates.
    damage_rect = gfx::SkIRectToRect(pending_damage_.getBounds());
    damage_rect.Intersect(output_rect);
  }

  const int kRenderPassId = 1;
  std::unique_ptr<cc::RenderPass> render_pass = cc::RenderPass::Create();
  render_pass->SetNew(kRenderPassId, output_rect, damage_rect,
                      gfx::Transform());

  gfx::Rect quad_rect = output_rect;
  cc::SharedQuadState* quad_state =
      render_pass->CreateAndAppendSharedQuadState();
  quad_state->quad_layer_rect = gfx::Rect(contents_surface_size);
  quad_state->visible_quad_layer_rect = quad_rect;
  quad_state->opacity = state_.alpha;

  cc::CompositorFrame frame;
  // If we commit while we don't have an active BeginFrame, we acknowledge a
  // manual one.
  if (current_begin_frame_ack_.sequence_number ==
      cc::BeginFrameArgs::kInvalidFrameNumber) {
    current_begin_frame_ack_ = cc::BeginFrameAck::CreateManualAckWithDamage();
  } else {
    current_begin_frame_ack_.has_damage = true;
  }
  frame.metadata.begin_frame_ack = current_begin_frame_ack_;
  frame.metadata.device_scale_factor = device_scale_factor_;

  if (current_resource_.id) {
    // Texture quad is only needed if buffer is not fully transparent.
    if (state_.alpha) {
      cc::TextureDrawQuad* texture_quad =
          render_pass->CreateAndAppendDrawQuad<cc::TextureDrawQuad>();
      float vertex_opacity[4] = {1.0, 1.0, 1.0, 1.0};
      gfx::Rect opaque_rect;
      if (!current_resource_has_alpha_ ||
          state_.blend_mode == SkBlendMode::kSrc ||
          state_.opaque_region.contains(gfx::RectToSkIRect(quad_rect))) {
        opaque_rect = quad_rect;
      } else if (state_.opaque_region.isRect()) {
        opaque_rect = gfx::SkIRectToRect(state_.opaque_region.getBounds());
      }

      texture_quad->SetNew(quad_state, quad_rect, opaque_rect, quad_rect,
                           current_resource_.id, true, uv_top_left,
                           uv_bottom_right, SK_ColorTRANSPARENT, vertex_opacity,
                           false, false, state_.only_visible_on_secure_output);
      if (current_resource_.is_overlay_candidate)
        texture_quad->set_resource_size_in_pixels(current_resource_.size);
      frame.resource_list.push_back(current_resource_);
    }
  } else {
    cc::SolidColorDrawQuad* solid_quad =
        render_pass->CreateAndAppendDrawQuad<cc::SolidColorDrawQuad>();
    solid_quad->SetNew(quad_state, quad_rect, quad_rect, SK_ColorBLACK, false);
  }

  frame.render_pass_list.push_back(std::move(render_pass));
  compositor_frame_sink_holder_->GetCompositorFrameSink()
      ->SubmitCompositorFrame(std::move(frame));
}

}  // namespace exo
