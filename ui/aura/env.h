// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_ENV_H_
#define UI_AURA_ENV_H_

#include <memory>

#include "base/macros.h"
#include "base/observer_list.h"
#include "base/supports_user_data.h"
#include "ui/aura/aura_export.h"
#include "ui/base/dragdrop/os_exchange_data_provider_factory.h"
#include "ui/events/event_handler.h"
#include "ui/events/event_target.h"
#include "ui/gfx/geometry/point.h"

#if defined(USE_OZONE)
#include "ui/ozone/public/client_native_pixmap_factory_ozone.h"

namespace gfx {
class ClientNativePixmapFactory;
}
#endif

namespace ui {
class ContextFactory;
class ContextFactoryPrivate;
class PlatformEventSource;
}
namespace aura {
namespace test {
class EnvTestHelper;
}

class EnvInputStateController;
class EnvObserver;
class InputStateLookup;
class MusMouseLocationUpdater;
class Window;
class WindowPort;
class WindowTreeClient;
class WindowTreeHost;

// A singleton object that tracks general state within Aura.
class AURA_EXPORT Env : public ui::EventTarget,
                        public ui::OSExchangeDataProviderFactory::Factory,
                        public base::SupportsUserData {
 public:
  enum class Mode {
    // Classic aura.
    LOCAL,

    // Aura with a backend of mus.
    MUS,
  };

  ~Env() override;

  // NOTE: if you pass in Mode::MUS it is expected that you call
  // SetWindowTreeClient() before any windows are created.
  static std::unique_ptr<Env> CreateInstance(Mode mode = Mode::LOCAL);
  static Env* GetInstance();
  static Env* GetInstanceDontCreate();

  Mode mode() const { return mode_; }

  // Called internally to create the appropriate WindowPort implementation.
  std::unique_ptr<WindowPort> CreateWindowPort(Window* window);

  void AddObserver(EnvObserver* observer);
  void RemoveObserver(EnvObserver* observer);

  EnvInputStateController* env_controller() const {
    return env_controller_.get();
  }

  int mouse_button_flags() const { return mouse_button_flags_; }
  void set_mouse_button_flags(int mouse_button_flags) {
    mouse_button_flags_ = mouse_button_flags;
  }
  // Returns true if a mouse button is down. This may query the native OS,
  // otherwise it uses |mouse_button_flags_|.
  bool IsMouseButtonDown() const;

  // Gets/sets the last mouse location seen in a mouse event in the screen
  // coordinates.
  const gfx::Point& last_mouse_location() const;
  void set_last_mouse_location(const gfx::Point& last_mouse_location) {
    last_mouse_location_ = last_mouse_location;
  }

  // Whether any touch device is currently down.
  bool is_touch_down() const { return is_touch_down_; }
  void set_touch_down(bool value) { is_touch_down_ = value; }

  void set_context_factory(ui::ContextFactory* context_factory) {
    context_factory_ = context_factory;
  }
  ui::ContextFactory* context_factory() { return context_factory_; }

  void set_context_factory_private(
      ui::ContextFactoryPrivate* context_factory_private) {
    context_factory_private_ = context_factory_private;
  }
  ui::ContextFactoryPrivate* context_factory_private() {
    return context_factory_private_;
  }

  // See CreateInstance() for description.
  void SetWindowTreeClient(WindowTreeClient* window_tree_client);
  bool HasWindowTreeClient() const { return window_tree_client_ != nullptr; }

 private:
  friend class test::EnvTestHelper;
  friend class EventInjector;
  friend class MusMouseLocationUpdater;
  friend class Window;
  friend class WindowTreeClient;  // For call to WindowTreeClientDestroyed().
  friend class WindowTreeHost;

  explicit Env(Mode mode);

  void Init();

  // After calling this method, all OSExchangeDataProvider instances will be
  // Mus instances. We can't do this work in Init(), because our mode may
  // changed via the EnvTestHelper.
  void EnableMusOSExchangeDataProvider();

  // Called by the Window when it is initialized. Notifies observers.
  void NotifyWindowInitialized(Window* window);

  // Called by the WindowTreeHost when it is initialized. Notifies observers.
  void NotifyHostInitialized(WindowTreeHost* host);

  // Invoked by WindowTreeHost when it is activated. Notifies observers.
  void NotifyHostActivated(WindowTreeHost* host);

  void WindowTreeClientDestroyed(WindowTreeClient* client);

  // Overridden from ui::EventTarget:
  bool CanAcceptEvent(const ui::Event& event) override;
  ui::EventTarget* GetParentTarget() override;
  std::unique_ptr<ui::EventTargetIterator> GetChildIterator() const override;
  ui::EventTargeter* GetEventTargeter() override;

  // Overridden from ui::OSExchangeDataProviderFactory::Factory:
  std::unique_ptr<ui::OSExchangeData::Provider> BuildProvider() override;

  // This is not const for tests, which may share Env across tests and so needs
  // to reset the value.
  Mode mode_;

  // Intentionally not exposed publicly. Someday we might want to support
  // multiple WindowTreeClients. Use EnvTestHelper in tests. This is set to null
  // during shutdown.
  WindowTreeClient* window_tree_client_ = nullptr;

  base::ObserverList<EnvObserver> observers_;

  std::unique_ptr<EnvInputStateController> env_controller_;
  int mouse_button_flags_;
  // Location of last mouse event, in screen coordinates.
  mutable gfx::Point last_mouse_location_;
  bool is_touch_down_;
  bool get_last_mouse_location_from_mus_;
  // This may be set to true in tests to force using |last_mouse_location_|
  // rather than querying WindowTreeClient.
  bool always_use_last_mouse_location_ = false;
  // Whether we set ourselves as the OSExchangeDataProviderFactory.
  bool is_os_exchange_data_provider_factory_ = false;

  std::unique_ptr<InputStateLookup> input_state_lookup_;
  std::unique_ptr<ui::PlatformEventSource> event_source_;

#if defined(USE_OZONE)
  // Factory for pixmaps that can use be transported from the client to the GPU
  // process using a low-level ozone-provided platform specific mechanism.
  std::unique_ptr<gfx::ClientNativePixmapFactory> native_pixmap_factory_;
#endif

  ui::ContextFactory* context_factory_;
  ui::ContextFactoryPrivate* context_factory_private_;

  // This is set to true when the WindowTreeClient is destroyed. It triggers
  // creating a different WindowPort implementation.
  bool in_mus_shutdown_ = false;

  DISALLOW_COPY_AND_ASSIGN(Env);
};

}  // namespace aura

#endif  // UI_AURA_ENV_H_
