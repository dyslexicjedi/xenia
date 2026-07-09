/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_MAC_H_
#define XENIA_UI_WINDOW_MAC_H_

#include <cstdint>
#include <memory>
#include <string>

#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

#ifdef __OBJC__
@class NSWindow;
@class XeMacMetalView;
@class XeMacWindowDelegate;
#else
typedef void NSWindow;
typedef void XeMacMetalView;
typedef void XeMacWindowDelegate;
#endif

namespace xe {
namespace ui {

class MacWindow : public Window {
  using super = Window;

 public:
  MacWindow(WindowedAppContext& app_context, const std::string_view title,
            uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~MacWindow() override;

  // Callbacks from the Objective-C view and window delegate. Native events are
  // translated to plain arguments in the Objective-C classes so these can be
  // declared without Objective-C types.

  enum class NativeMouseEvent {
    kDown,
    kUp,
    kMove,
    kWheel,
  };

  // Returns whether AppKit should proceed with closing the native window.
  bool HandleWindowShouldClose();
  void HandleSizeOrScaleUpdate();
  void HandleFocusUpdate(bool new_has_focus);
  void HandleFullscreenTransition(bool new_fullscreen);
  void HandlePaint();
  // x and y are in view points (top-left origin), converted to physical pixels
  // internally.
  void HandleMouse(NativeMouseEvent type, MouseEvent::Button button, float x,
                   float y, float scroll_x, float scroll_y);
  void HandleKey(uint16_t keycode, uint32_t characters_first_unit,
                 bool shift_pressed, bool ctrl_pressed, bool alt_pressed,
                 bool super_pressed, bool is_up);

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  uint32_t GetLatestDpiImpl() const override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void FocusImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  // Keeps the CAMetalLayer's contentsScale and drawableSize in sync with the
  // view size and the display backing scale factor.
  void UpdateLayerDrawableSize();
  // Detaches the Objective-C objects from this Window (so no more native
  // callbacks arrive) and releases them, closing the native window if
  // requested (not needed when AppKit itself is performing the close).
  void DetachAndReleaseNative(bool close_native_window);

  NSWindow* window_ = nullptr;
  XeMacMetalView* view_ = nullptr;
  XeMacWindowDelegate* delegate_ = nullptr;

  // Last known backing scale factor in medium-DPI units, for GetLatestDpiImpl
  // when the window is closed (0 = unknown, treated as medium DPI).
  uint32_t latest_dpi_ = 0;
};

// Menus are not implemented on macOS yet - this only stores the item tree.
// TODO(macos): Build an NSMenu main menu from the MenuItem tree.
class MacMenuItem final : public MenuItem {
 public:
  MacMenuItem(Type type, const std::string& text, const std::string& hotkey,
              std::function<void()> callback)
      : MenuItem(type, text, hotkey, std::move(callback)) {}
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_MAC_H_
