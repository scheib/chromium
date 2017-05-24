// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_MUS_WINDOW_TREE_CLIENT_H_
#define UI_AURA_MUS_WINDOW_TREE_CLIENT_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/atomicops.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/single_thread_task_runner.h"
#include "cc/surfaces/local_surface_id_allocator.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/ui/public/interfaces/window_tree.mojom.h"
#include "ui/aura/aura_export.h"
#include "ui/aura/client/transient_window_client_observer.h"
#include "ui/aura/mus/capture_synchronizer_delegate.h"
#include "ui/aura/mus/drag_drop_controller_host.h"
#include "ui/aura/mus/focus_synchronizer_delegate.h"
#include "ui/aura/mus/mus_types.h"
#include "ui/aura/mus/window_manager_delegate.h"
#include "ui/aura/mus/window_tree_host_mus_delegate.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/compositor_observer.h"

namespace base {
class Thread;
}

namespace discardable_memory {
class ClientDiscardableSharedMemoryManager;
}

namespace display {
class Display;
}

namespace gfx {
class Insets;
}

namespace service_manager {
class Connector;
}

namespace ui {
class ContextFactory;
class Gpu;
struct PropertyData;
}

namespace aura {
class CaptureSynchronizer;
class DragDropControllerMus;
class FocusSynchronizer;
class InFlightBoundsChange;
class InFlightChange;
class InFlightFocusChange;
class InFlightPropertyChange;
class InFlightVisibleChange;
class MusContextFactory;
class WindowMus;
class WindowPortMus;
class WindowTreeClientDelegate;
class WindowTreeClientPrivate;
class WindowTreeClientObserver;
class WindowTreeClientTestObserver;
class WindowTreeHostMus;

using EventResultCallback = base::Callback<void(ui::mojom::EventResult)>;

// Manages the connection with mus.
//
// WindowTreeClient is owned by the creator. Generally when the delegate gets
// one of OnEmbedRootDestroyed() or OnLostConnection() it should delete the
// WindowTreeClient.
//
// When WindowTreeClient is deleted all windows are deleted (and observers
// notified).
class AURA_EXPORT WindowTreeClient
    : NON_EXPORTED_BASE(public ui::mojom::WindowTreeClient),
      NON_EXPORTED_BASE(public ui::mojom::WindowManager),
      public CaptureSynchronizerDelegate,
      public FocusSynchronizerDelegate,
      public DragDropControllerHost,
      public WindowManagerClient,
      public WindowTreeHostMusDelegate,
      public client::TransientWindowClientObserver,
      public ui::CompositorObserver {
 public:
  // |create_discardable_memory| If it is true, WindowTreeClient will setup the
  // dicardable shared memory manager for this process. In some test, more than
  // one WindowTreeClient will be created, so we need pass false to avoid
  // setup the discardable shared memory manager more than once.
  WindowTreeClient(
      service_manager::Connector* connector,
      WindowTreeClientDelegate* delegate,
      WindowManagerDelegate* window_manager_delegate = nullptr,
      ui::mojom::WindowTreeClientRequest request = nullptr,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner = nullptr,
      bool create_discardable_memory = true);
  ~WindowTreeClient() override;

  // Establishes the connection by way of the WindowTreeFactory.
  void ConnectViaWindowTreeFactory();

  // Establishes the connection by way of WindowManagerWindowTreeFactory.
  // See mojom for details on |automatically_create_display_roots|.
  void ConnectAsWindowManager(bool automatically_create_display_roots = true);

  service_manager::Connector* connector() { return connector_; }
  ui::Gpu* gpu() { return gpu_.get(); }
  CaptureSynchronizer* capture_synchronizer() {
    return capture_synchronizer_.get();
  }
  FocusSynchronizer* focus_synchronizer() { return focus_synchronizer_.get(); }

  bool connected() const { return tree_ != nullptr; }
  ClientSpecificId client_id() const { return client_id_; }

  void SetCanFocus(Window* window, bool can_focus);
  void SetCanAcceptDrops(WindowMus* window, bool can_accept_drops);
  void SetEventTargetingPolicy(WindowMus* window,
                               ui::mojom::EventTargetingPolicy policy);
  void SetCursor(WindowMus* window,
                 const ui::CursorData& old_cursor,
                 const ui::CursorData& new_cursor);
  void SetWindowTextInputState(WindowMus* window,
                               mojo::TextInputStatePtr state);
  void SetImeVisibility(WindowMus* window,
                        bool visible,
                        mojo::TextInputStatePtr state);

  // Embeds a new client in |window|. |flags| is a bitmask of the values defined
  // by kEmbedFlag*; 0 gives default behavior. |callback| is called to indicate
  // whether the embedding succeeded or failed and may be called immediately if
  // the embedding is known to fail.
  void Embed(Window* window,
             ui::mojom::WindowTreeClientPtr client,
             uint32_t flags,
             const ui::mojom::WindowTree::EmbedCallback& callback);

  void AttachCompositorFrameSink(
      Id window_id,
      cc::mojom::MojoCompositorFrameSinkRequest compositor_frame_sink,
      cc::mojom::MojoCompositorFrameSinkClientPtr client);

  bool IsRoot(WindowMus* window) const { return roots_.count(window) > 0; }

  // Returns the root of this connection.
  std::set<Window*> GetRoots();

  // Returns true if the specified window was created by this client.
  bool WasCreatedByThisClient(const WindowMus* window) const;

  // Returns the current location of the mouse on screen. Note: this method may
  // race the asynchronous initialization; but in that case we return (0, 0).
  gfx::Point GetCursorScreenPoint();

  // See description in window_tree.mojom. When an existing pointer watcher is
  // updated or cleared then any future events from the server for that watcher
  // will be ignored.
  void StartPointerWatcher(bool want_moves);
  void StopPointerWatcher();

  void AddObserver(WindowTreeClientObserver* observer);
  void RemoveObserver(WindowTreeClientObserver* observer);

  void AddTestObserver(WindowTreeClientTestObserver* observer);
  void RemoveTestObserver(WindowTreeClientTestObserver* observer);

 private:
  friend class InFlightBoundsChange;
  friend class InFlightFocusChange;
  friend class InFlightPropertyChange;
  friend class InFlightVisibleChange;
  friend class WindowPortMus;
  friend class WindowTreeClientPrivate;

  enum class Origin {
    CLIENT,
    SERVER,
  };

  using IdToWindowMap = std::map<Id, WindowMus*>;

  // TODO(sky): this assumes change_ids never wrap, which is a bad assumption.
  using InFlightMap = std::map<uint32_t, std::unique_ptr<InFlightChange>>;

  void RegisterWindowMus(WindowMus* window);

  WindowMus* GetWindowByServerId(Id id);

  bool IsWindowKnown(aura::Window* window);

  // Returns the oldest InFlightChange that matches |change|.
  InFlightChange* GetOldestInFlightChangeMatching(const InFlightChange& change);

  // See InFlightChange for details on how InFlightChanges are used.
  uint32_t ScheduleInFlightChange(std::unique_ptr<InFlightChange> change);

  // Returns true if there is an InFlightChange that matches |change|. If there
  // is an existing change SetRevertValueFrom() is invoked on it. Returns false
  // if there is no InFlightChange matching |change|.
  // See InFlightChange for details on how InFlightChanges are used.
  bool ApplyServerChangeToExistingInFlightChange(const InFlightChange& change);

  void BuildWindowTree(const std::vector<ui::mojom::WindowDataPtr>& windows);

  // If the window identified by |window_data| doesn't exist a new window is
  // created, otherwise the existing window is updated based on |window_data|.
  void CreateOrUpdateWindowFromWindowData(
      const ui::mojom::WindowData& window_data);

  // Creates a WindowPortMus from the server side data.
  std::unique_ptr<WindowPortMus> CreateWindowPortMus(
      const ui::mojom::WindowData& window_data,
      WindowMusType window_mus_type);

  // Sets local properties on the associated Window from the server properties.
  void SetLocalPropertiesFromServerProperties(
      WindowMus* window,
      const ui::mojom::WindowData& window_data);

  // Creates a new WindowTreeHostMus.
  std::unique_ptr<WindowTreeHostMus> CreateWindowTreeHost(
      WindowMusType window_mus_type,
      const ui::mojom::WindowData& window_data,
      int64_t display_id,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id =
          base::nullopt);

  WindowMus* NewWindowFromWindowData(WindowMus* parent,
                                     const ui::mojom::WindowData& window_data);

  // Sets the ui::mojom::WindowTree implementation.
  void SetWindowTree(ui::mojom::WindowTreePtr window_tree_ptr);

  // Called when the connection to the server is established.
  void WindowTreeConnectionEstablished(ui::mojom::WindowTree* window_tree);

  // Called when the ui::mojom::WindowTree connection is lost, deletes this.
  void OnConnectionLost();

  // Called when a Window property changes. If |key| is handled internally
  // (maps to a function on WindowTree) returns true.
  bool HandleInternalPropertyChanged(WindowMus* window,
                                     const void* key,
                                     int64_t old_value);

  // OnEmbed() calls into this. Exposed as a separate function for testing.
  void OnEmbedImpl(ui::mojom::WindowTree* window_tree,
                   ClientSpecificId client_id,
                   ui::mojom::WindowDataPtr root_data,
                   int64_t display_id,
                   Id focused_window_id,
                   bool drawn,
                   const base::Optional<cc::LocalSurfaceId>& local_surface_id);

  // Called once mus acks the call to SetDisplayRoot().
  void OnSetDisplayRootDone(bool success);

  // Called by WmNewDisplayAdded().
  WindowTreeHostMus* WmNewDisplayAddedImpl(
      const display::Display& display,
      ui::mojom::WindowDataPtr root_data,
      bool parent_drawn,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id);

  std::unique_ptr<EventResultCallback> CreateEventResultCallback(
      int32_t event_id);

  void OnReceivedCursorLocationMemory(mojo::ScopedSharedBufferHandle handle);

  // Called when a property needs to change as the result of a change in the
  // server, or the server failing to accept a change.
  void SetWindowBoundsFromServer(
      WindowMus* window,
      const gfx::Rect& revert_bounds_in_pixels,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id);
  void SetWindowVisibleFromServer(WindowMus* window, bool visible);

  // Called from OnWindowMusBoundsChanged() and SetRootWindowBounds().
  void ScheduleInFlightBoundsChange(WindowMus* window,
                                    const gfx::Rect& old_bounds,
                                    const gfx::Rect& new_bounds);

  // Following are called from WindowMus.
  void OnWindowMusCreated(WindowMus* window);
  void OnWindowMusDestroyed(WindowMus* window, Origin origin);
  void OnWindowMusBoundsChanged(WindowMus* window,
                                const gfx::Rect& old_bounds,
                                const gfx::Rect& new_bounds);
  void OnWindowMusAddChild(WindowMus* parent, WindowMus* child);
  void OnWindowMusRemoveChild(WindowMus* parent, WindowMus* child);
  void OnWindowMusMoveChild(WindowMus* parent,
                            size_t current_index,
                            size_t dest_index);
  void OnWindowMusSetVisible(WindowMus* window, bool visible);
  std::unique_ptr<ui::PropertyData> OnWindowMusWillChangeProperty(
      WindowMus* window,
      const void* key);
  void OnWindowMusPropertyChanged(WindowMus* window,
                                  const void* key,
                                  int64_t old_value,
                                  std::unique_ptr<ui::PropertyData> data);

  // Callback passed from WmPerformMoveLoop().
  void OnWmMoveLoopCompleted(uint32_t change_id, bool completed);

  // Overridden from WindowTreeClient:
  void OnEmbed(
      ClientSpecificId client_id,
      ui::mojom::WindowDataPtr root,
      ui::mojom::WindowTreePtr tree,
      int64_t display_id,
      Id focused_window_id,
      bool drawn,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id) override;
  void OnEmbeddedAppDisconnected(Id window_id) override;
  void OnUnembed(Id window_id) override;
  void OnCaptureChanged(Id new_capture_window_id,
                        Id old_capture_window_id) override;
  void OnFrameSinkIdAllocated(Id window_id,
                              const cc::FrameSinkId& frame_sink_id) override;
  void OnTopLevelCreated(
      uint32_t change_id,
      ui::mojom::WindowDataPtr data,
      int64_t display_id,
      bool drawn,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id) override;
  void OnWindowBoundsChanged(
      Id window_id,
      const gfx::Rect& old_bounds,
      const gfx::Rect& new_bounds,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id) override;
  void OnClientAreaChanged(
      uint32_t window_id,
      const gfx::Insets& new_client_area,
      const std::vector<gfx::Rect>& new_additional_client_areas) override;
  void OnTransientWindowAdded(uint32_t window_id,
                              uint32_t transient_window_id) override;
  void OnTransientWindowRemoved(uint32_t window_id,
                                uint32_t transient_window_id) override;
  void OnWindowHierarchyChanged(
      Id window_id,
      Id old_parent_id,
      Id new_parent_id,
      std::vector<ui::mojom::WindowDataPtr> windows) override;
  void OnWindowReordered(Id window_id,
                         Id relative_window_id,
                         ui::mojom::OrderDirection direction) override;
  void OnWindowDeleted(Id window_id) override;
  void OnWindowVisibilityChanged(Id window_id, bool visible) override;
  void OnWindowOpacityChanged(Id window_id,
                              float old_opacity,
                              float new_opacity) override;
  void OnWindowParentDrawnStateChanged(Id window_id, bool drawn) override;
  void OnWindowSharedPropertyChanged(
      Id window_id,
      const std::string& name,
      const base::Optional<std::vector<uint8_t>>& transport_data) override;
  void OnWindowInputEvent(uint32_t event_id,
                          Id window_id,
                          int64_t display_id,
                          std::unique_ptr<ui::Event> event,
                          bool matches_pointer_watcher) override;
  void OnPointerEventObserved(std::unique_ptr<ui::Event> event,
                              uint32_t window_id,
                              int64_t display_id) override;
  void OnWindowFocused(Id focused_window_id) override;
  void OnWindowCursorChanged(Id window_id, ui::CursorData cursor) override;
  void OnWindowSurfaceChanged(Id window_id,
                              const cc::SurfaceInfo& surface_info) override;
  void OnDragDropStart(
      const std::unordered_map<std::string, std::vector<uint8_t>>& mime_data)
      override;
  void OnDragEnter(Id window_id,
                   uint32_t event_flags,
                   const gfx::Point& position,
                   uint32_t effect_bitmask,
                   const OnDragEnterCallback& callback) override;
  void OnDragOver(Id window_id,
                  uint32_t event_flags,
                  const gfx::Point& position,
                  uint32_t effect_bitmask,
                  const OnDragOverCallback& callback) override;
  void OnDragLeave(Id window_id) override;
  void OnCompleteDrop(Id window_id,
                      uint32_t event_flags,
                      const gfx::Point& position,
                      uint32_t effect_bitmask,
                      const OnCompleteDropCallback& callback) override;
  void OnPerformDragDropCompleted(uint32_t window,
                                  bool success,
                                  uint32_t action_taken) override;
  void OnDragDropDone() override;
  void OnChangeCompleted(uint32_t change_id, bool success) override;
  void RequestClose(uint32_t window_id) override;
  void GetWindowManager(
      mojo::AssociatedInterfaceRequest<WindowManager> internal) override;

  // Overridden from WindowManager:
  void OnConnect(ClientSpecificId client_id) override;
  void WmNewDisplayAdded(
      const display::Display& display,
      ui::mojom::WindowDataPtr root_data,
      bool parent_drawn,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id) override;
  void WmDisplayRemoved(int64_t display_id) override;
  void WmDisplayModified(const display::Display& display) override;
  void WmSetBounds(uint32_t change_id,
                   Id window_id,
                   const gfx::Rect& transit_bounds_in_pixels) override;
  void WmSetProperty(
      uint32_t change_id,
      Id window_id,
      const std::string& name,
      const base::Optional<std::vector<uint8_t>>& transit_data) override;
  void WmSetModalType(Id window_id, ui::ModalType type) override;
  void WmSetCanFocus(Id window_id, bool can_focus) override;
  void WmCreateTopLevelWindow(
      uint32_t change_id,
      ClientSpecificId requesting_client_id,
      const std::unordered_map<std::string, std::vector<uint8_t>>&
          transport_properties) override;
  void WmClientJankinessChanged(ClientSpecificId client_id,
                                bool janky) override;
  void WmBuildDragImage(const gfx::Point& screen_location,
                        const SkBitmap& drag_image,
                        const gfx::Vector2d& drag_image_offset,
                        ui::mojom::PointerKind source) override;
  void WmMoveDragImage(const gfx::Point& screen_location,
                       const WmMoveDragImageCallback& callback) override;
  void WmDestroyDragImage() override;
  void WmPerformMoveLoop(uint32_t change_id,
                         Id window_id,
                         ui::mojom::MoveLoopSource source,
                         const gfx::Point& cursor_location) override;
  void WmCancelMoveLoop(uint32_t window_id) override;
  void WmDeactivateWindow(Id window_id) override;
  void WmStackAbove(uint32_t change_id, Id above_id, Id below_id) override;
  void WmStackAtTop(uint32_t change_id, uint32_t window_id) override;
  void OnAccelerator(uint32_t ack_id,
                     uint32_t accelerator_id,
                     std::unique_ptr<ui::Event> event) override;

  // Overridden from WindowManagerClient:
  void SetFrameDecorationValues(
      ui::mojom::FrameDecorationValuesPtr values) override;
  void SetNonClientCursor(Window* window,
                          const ui::CursorData& cursor) override;
  void AddAccelerators(std::vector<ui::mojom::WmAcceleratorPtr> accelerators,
                       const base::Callback<void(bool)>& callback) override;
  void RemoveAccelerator(uint32_t id) override;
  void AddActivationParent(Window* window) override;
  void RemoveActivationParent(Window* window) override;
  void ActivateNextWindow() override;
  void SetExtendedHitArea(Window* window, const gfx::Insets& hit_area) override;
  void LockCursor() override;
  void UnlockCursor() override;
  void SetCursorVisible(bool visible) override;
  void SetGlobalOverrideCursor(base::Optional<ui::CursorData> cursor) override;
  void RequestClose(Window* window) override;
  bool WaitForInitialDisplays() override;
  WindowTreeHostMusInitParams CreateInitParamsForNewDisplay() override;

  // Overriden from WindowTreeHostMusDelegate:
  void OnWindowTreeHostBoundsWillChange(WindowTreeHostMus* window_tree_host,
                                        const gfx::Rect& bounds) override;
  void OnWindowTreeHostClientAreaWillChange(
      WindowTreeHostMus* window_tree_host,
      const gfx::Insets& client_area,
      const std::vector<gfx::Rect>& additional_client_areas) override;
  void OnWindowTreeHostHitTestMaskWillChange(
      WindowTreeHostMus* window_tree_host,
      const base::Optional<gfx::Rect>& mask_rect) override;
  void OnWindowTreeHostSetOpacity(WindowTreeHostMus* window_tree_host,
                                  float opacity) override;
  void OnWindowTreeHostDeactivateWindow(
      WindowTreeHostMus* window_tree_host) override;
  void OnWindowTreeHostStackAbove(WindowTreeHostMus* window_tree_host,
                                  Window* window) override;
  void OnWindowTreeHostStackAtTop(WindowTreeHostMus* window_tree_host) override;
  void OnWindowTreeHostPerformWindowMove(
      WindowTreeHostMus* window_tree_host,
      ui::mojom::MoveLoopSource mus_source,
      const gfx::Point& cursor_location,
      const base::Callback<void(bool)>& callback) override;
  void OnWindowTreeHostCancelWindowMove(
      WindowTreeHostMus* window_tree_host) override;
  std::unique_ptr<WindowPortMus> CreateWindowPortForTopLevel(
      const std::map<std::string, std::vector<uint8_t>>* properties) override;
  void OnWindowTreeHostCreated(WindowTreeHostMus* window_tree_host) override;

  // Override from client::TransientWindowClientObserver:
  void OnTransientChildWindowAdded(Window* parent,
                                   Window* transient_child) override;
  void OnTransientChildWindowRemoved(Window* parent,
                                     Window* transient_child) override;
  void OnWillRestackTransientChildAbove(Window* parent,
                                        Window* transient_child) override;
  void OnDidRestackTransientChildAbove(Window* parent,
                                       Window* transient_child) override;

  // Overriden from DragDropControllerHost:
  uint32_t CreateChangeIdForDrag(WindowMus* window) override;

  // Overrided from CaptureSynchronizerDelegate:
  uint32_t CreateChangeIdForCapture(WindowMus* window) override;

  // Overrided from FocusSynchronizerDelegate:
  uint32_t CreateChangeIdForFocus(WindowMus* window) override;

  // Overrided from CompositorObserver:
  void OnCompositingDidCommit(ui::Compositor* compositor) override;
  void OnCompositingStarted(ui::Compositor* compositor,
                            base::TimeTicks start_time) override;
  void OnCompositingEnded(ui::Compositor* compositor) override;
  void OnCompositingLockStateChanged(ui::Compositor* compositor) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

  // The one int in |cursor_location_mapping_|. When we read from this
  // location, we must always read from it atomically.
  base::subtle::Atomic32* cursor_location_memory() {
    return reinterpret_cast<base::subtle::Atomic32*>(
        cursor_location_mapping_.get());
  }

  // This may be null in tests.
  service_manager::Connector* connector_;

  // This is set once and only once when we get OnEmbed(). It gives the unique
  // id for this client.
  ClientSpecificId client_id_;

  // Id assigned to the next window created.
  ClientSpecificId next_window_id_;

  // Id used for the next change id supplied to the server.
  uint32_t next_change_id_;
  InFlightMap in_flight_map_;

  WindowTreeClientDelegate* delegate_;

  WindowManagerDelegate* window_manager_delegate_;

  std::set<WindowMus*> roots_;

  IdToWindowMap windows_;
  std::map<ClientSpecificId, std::set<Window*>> embedded_windows_;

  std::unique_ptr<CaptureSynchronizer> capture_synchronizer_;

  std::unique_ptr<FocusSynchronizer> focus_synchronizer_;

  mojo::Binding<ui::mojom::WindowTreeClient> binding_;
  ui::mojom::WindowTreePtr tree_ptr_;
  // Typically this is the value contained in |tree_ptr_|, but tests may
  // directly set this.
  ui::mojom::WindowTree* tree_;

  // Set to true if OnEmbed() was received.
  bool is_from_embed_ = false;

  bool in_destructor_;

  bool enable_surface_synchronization_ = false;

  // A mapping to shared memory that is one 32 bit integer long. The window
  // server uses this to let us synchronously read the cursor location.
  mojo::ScopedSharedBufferMapping cursor_location_mapping_;

  base::ObserverList<WindowTreeClientObserver> observers_;

  std::unique_ptr<mojo::AssociatedBinding<ui::mojom::WindowManager>>
      window_manager_internal_;
  ui::mojom::WindowManagerClientAssociatedPtr window_manager_internal_client_;
  // Normally the same as |window_manager_internal_client_|. Tests typically
  // run without a service_manager::Connector, which means this (and
  // |window_manager_internal_client_|) are null. Tests that need to test
  // WindowManagerClient set this, but not |window_manager_internal_client_|.
  ui::mojom::WindowManagerClient* window_manager_client_ = nullptr;

  bool has_pointer_watcher_ = false;

  // The current change id for the client.
  uint32_t current_move_loop_change_ = 0u;

  // Callback executed when a move loop initiated by PerformWindowMove() is
  // completed.
  base::Callback<void(bool)> on_current_move_finished_;

  // The current change id for the window manager.
  uint32_t current_wm_move_loop_change_ = 0u;
  Id current_wm_move_loop_window_id_ = 0u;

  std::unique_ptr<DragDropControllerMus> drag_drop_controller_;

  base::ObserverList<WindowTreeClientTestObserver> test_observers_;

  // IO thread for GPU and discardable shared memory IPC.
  std::unique_ptr<base::Thread> io_thread_;

  std::unique_ptr<ui::Gpu> gpu_;
  std::unique_ptr<MusContextFactory> compositor_context_factory_;

  std::unique_ptr<discardable_memory::ClientDiscardableSharedMemoryManager>
      discardable_shared_memory_manager_;

  // If |compositor_context_factory_| is installed on Env, then this is the
  // ContextFactory that was set on Env originally.
  ui::ContextFactory* initial_context_factory_ = nullptr;

  // Set to true once OnWmDisplayAdded() is called.
  bool got_initial_displays_ = false;

  // Set to true if the next CompositorFrame will block on a new child surface.
  bool synchronizing_with_child_on_next_frame_ = false;

  // Set to true if this WindowTreeClient is currently holding pointer moves.
  bool holding_pointer_moves_ = false;

  gfx::Insets normal_client_area_insets_;

  bool in_shutdown_ = false;

  base::WeakPtrFactory<WindowTreeClient> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WindowTreeClient);
};

}  // namespace aura

#endif  // UI_AURA_MUS_WINDOW_TREE_CLIENT_H_
