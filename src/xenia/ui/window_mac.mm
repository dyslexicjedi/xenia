/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window_mac.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cmath>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/surface_mac.h"
#include "xenia/ui/virtual_key.h"

namespace {

// macOS virtual keycodes (HIToolbox Events.h kVK_* values, stable since
// classic Mac OS) to Windows-like Xenia virtual keys.
xe::ui::VirtualKey TranslateKeycode(uint16_t keycode) {
  using xe::ui::VirtualKey;
  switch (keycode) {
    case 0x00:  // kVK_ANSI_A
      return VirtualKey::kA;
    case 0x01:  // kVK_ANSI_S
      return VirtualKey::kS;
    case 0x02:  // kVK_ANSI_D
      return VirtualKey::kD;
    case 0x03:  // kVK_ANSI_F
      return VirtualKey::kF;
    case 0x04:  // kVK_ANSI_H
      return VirtualKey::kH;
    case 0x05:  // kVK_ANSI_G
      return VirtualKey::kG;
    case 0x06:  // kVK_ANSI_Z
      return VirtualKey::kZ;
    case 0x07:  // kVK_ANSI_X
      return VirtualKey::kX;
    case 0x08:  // kVK_ANSI_C
      return VirtualKey::kC;
    case 0x09:  // kVK_ANSI_V
      return VirtualKey::kV;
    case 0x0B:  // kVK_ANSI_B
      return VirtualKey::kB;
    case 0x0C:  // kVK_ANSI_Q
      return VirtualKey::kQ;
    case 0x0D:  // kVK_ANSI_W
      return VirtualKey::kW;
    case 0x0E:  // kVK_ANSI_E
      return VirtualKey::kE;
    case 0x0F:  // kVK_ANSI_R
      return VirtualKey::kR;
    case 0x10:  // kVK_ANSI_Y
      return VirtualKey::kY;
    case 0x11:  // kVK_ANSI_T
      return VirtualKey::kT;
    case 0x12:  // kVK_ANSI_1
      return VirtualKey::k1;
    case 0x13:  // kVK_ANSI_2
      return VirtualKey::k2;
    case 0x14:  // kVK_ANSI_3
      return VirtualKey::k3;
    case 0x15:  // kVK_ANSI_4
      return VirtualKey::k4;
    case 0x16:  // kVK_ANSI_6
      return VirtualKey::k6;
    case 0x17:  // kVK_ANSI_5
      return VirtualKey::k5;
    case 0x18:  // kVK_ANSI_Equal
      return VirtualKey::kOemPlus;
    case 0x19:  // kVK_ANSI_9
      return VirtualKey::k9;
    case 0x1A:  // kVK_ANSI_7
      return VirtualKey::k7;
    case 0x1B:  // kVK_ANSI_Minus
      return VirtualKey::kOemMinus;
    case 0x1C:  // kVK_ANSI_8
      return VirtualKey::k8;
    case 0x1D:  // kVK_ANSI_0
      return VirtualKey::k0;
    case 0x1E:  // kVK_ANSI_RightBracket
      return VirtualKey::kOem6;
    case 0x1F:  // kVK_ANSI_O
      return VirtualKey::kO;
    case 0x20:  // kVK_ANSI_U
      return VirtualKey::kU;
    case 0x21:  // kVK_ANSI_LeftBracket
      return VirtualKey::kOem4;
    case 0x22:  // kVK_ANSI_I
      return VirtualKey::kI;
    case 0x23:  // kVK_ANSI_P
      return VirtualKey::kP;
    case 0x24:  // kVK_Return
      return VirtualKey::kReturn;
    case 0x25:  // kVK_ANSI_L
      return VirtualKey::kL;
    case 0x26:  // kVK_ANSI_J
      return VirtualKey::kJ;
    case 0x27:  // kVK_ANSI_Quote
      return VirtualKey::kOem7;
    case 0x28:  // kVK_ANSI_K
      return VirtualKey::kK;
    case 0x29:  // kVK_ANSI_Semicolon
      return VirtualKey::kOem1;
    case 0x2A:  // kVK_ANSI_Backslash
      return VirtualKey::kOem5;
    case 0x2B:  // kVK_ANSI_Comma
      return VirtualKey::kOemComma;
    case 0x2C:  // kVK_ANSI_Slash
      return VirtualKey::kOem2;
    case 0x2D:  // kVK_ANSI_N
      return VirtualKey::kN;
    case 0x2E:  // kVK_ANSI_M
      return VirtualKey::kM;
    case 0x2F:  // kVK_ANSI_Period
      return VirtualKey::kOemPeriod;
    case 0x30:  // kVK_Tab
      return VirtualKey::kTab;
    case 0x31:  // kVK_Space
      return VirtualKey::kSpace;
    case 0x32:  // kVK_ANSI_Grave
      return VirtualKey::kOem3;
    case 0x33:  // kVK_Delete (backspace)
      return VirtualKey::kBack;
    case 0x35:  // kVK_Escape
      return VirtualKey::kEscape;
    case 0x36:  // kVK_RightCommand
      return VirtualKey::kRWin;
    case 0x37:  // kVK_Command
      return VirtualKey::kLWin;
    case 0x38:  // kVK_Shift
    case 0x3C:  // kVK_RightShift
      return VirtualKey::kShift;
    case 0x3A:  // kVK_Option
    case 0x3D:  // kVK_RightOption
      return VirtualKey::kMenu;
    case 0x3B:  // kVK_Control
    case 0x3E:  // kVK_RightControl
      return VirtualKey::kControl;
    case 0x60:  // kVK_F5
      return VirtualKey::kF5;
    case 0x61:  // kVK_F6
      return VirtualKey::kF6;
    case 0x62:  // kVK_F7
      return VirtualKey::kF7;
    case 0x63:  // kVK_F3
      return VirtualKey::kF3;
    case 0x64:  // kVK_F8
      return VirtualKey::kF8;
    case 0x65:  // kVK_F9
      return VirtualKey::kF9;
    case 0x67:  // kVK_F11
      return VirtualKey::kF11;
    case 0x6D:  // kVK_F10
      return VirtualKey::kF10;
    case 0x6F:  // kVK_F12
      return VirtualKey::kF12;
    case 0x73:  // kVK_Home
      return VirtualKey::kHome;
    case 0x74:  // kVK_PageUp
      return VirtualKey::kPrior;
    case 0x75:  // kVK_ForwardDelete
      return VirtualKey::kDelete;
    case 0x76:  // kVK_F4
      return VirtualKey::kF4;
    case 0x77:  // kVK_End
      return VirtualKey::kEnd;
    case 0x78:  // kVK_F2
      return VirtualKey::kF2;
    case 0x79:  // kVK_PageDown
      return VirtualKey::kNext;
    case 0x7A:  // kVK_F1
      return VirtualKey::kF1;
    case 0x7B:  // kVK_LeftArrow
      return VirtualKey::kLeft;
    case 0x7C:  // kVK_RightArrow
      return VirtualKey::kRight;
    case 0x7D:  // kVK_DownArrow
      return VirtualKey::kDown;
    case 0x7E:  // kVK_UpArrow
      return VirtualKey::kUp;
    default:
      return VirtualKey::kNone;
  }
}

}  // namespace

// The view backing the client area with a CAMetalLayer, forwarding input to
// the MacWindow. xenia_window is nulled when the MacWindow detaches - events
// arriving after that are dropped.
@interface XeMacMetalView : NSView {
 @public
  xe::ui::MacWindow* xenia_window;
}
@end

@implementation XeMacMetalView

- (CALayer*)makeBackingLayer {
  return [CAMetalLayer layer];
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)updateLayer {
  if (xenia_window) {
    xenia_window->HandlePaint();
  }
}

// Top-left origin like all the other Xenia windowing backends.
- (BOOL)isFlipped {
  return YES;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  if (xenia_window) {
    xenia_window->HandleSizeOrScaleUpdate();
  }
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  if (xenia_window) {
    xenia_window->HandleSizeOrScaleUpdate();
  }
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  for (NSTrackingArea* tracking_area in self.trackingAreas) {
    [self removeTrackingArea:tracking_area];
  }
  [self addTrackingArea:[[[NSTrackingArea alloc]
                            initWithRect:NSZeroRect
                                 options:NSTrackingMouseMoved |
                                         NSTrackingActiveInKeyWindow |
                                         NSTrackingInVisibleRect
                                   owner:self
                                userInfo:nil] autorelease]];
}

- (void)handleMouseEvent:(NSEvent*)event
                    type:(xe::ui::MacWindow::NativeMouseEvent)type
                  button:(xe::ui::MouseEvent::Button)button {
  if (!xenia_window) {
    return;
  }
  NSPoint location = [self convertPoint:event.locationInWindow fromView:nil];
  float scroll_x = 0.0f;
  float scroll_y = 0.0f;
  if (type == xe::ui::MacWindow::NativeMouseEvent::kWheel) {
    scroll_x = float(event.scrollingDeltaX);
    // NSEvent scrolling deltas are positive when the content should move down
    // (scrolling towards the top) - matching the positive-away-from-the-user
    // convention of MouseEvent.
    scroll_y = float(event.scrollingDeltaY);
    if (event.hasPreciseScrollingDeltas) {
      // Precise deltas are in points of content motion rather than in wheel
      // steps - approximate a wheel step as a common line height.
      scroll_x /= 16.0f;
      scroll_y /= 16.0f;
    }
  }
  xenia_window->HandleMouse(type, button, float(location.x), float(location.y),
                            scroll_x, scroll_y);
}

- (void)mouseDown:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kDown
                  button:xe::ui::MouseEvent::Button::kLeft];
}

- (void)mouseUp:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kUp
                  button:xe::ui::MouseEvent::Button::kLeft];
}

- (void)rightMouseDown:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kDown
                  button:xe::ui::MouseEvent::Button::kRight];
}

- (void)rightMouseUp:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kUp
                  button:xe::ui::MouseEvent::Button::kRight];
}

- (void)otherMouseDown:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kDown
                  button:xe::ui::MouseEvent::Button::kMiddle];
}

- (void)otherMouseUp:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kUp
                  button:xe::ui::MouseEvent::Button::kMiddle];
}

- (void)mouseMoved:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kMove
                  button:xe::ui::MouseEvent::Button::kNone];
}

- (void)mouseDragged:(NSEvent*)event {
  [self mouseMoved:event];
}

- (void)rightMouseDragged:(NSEvent*)event {
  [self mouseMoved:event];
}

- (void)otherMouseDragged:(NSEvent*)event {
  [self mouseMoved:event];
}

- (void)scrollWheel:(NSEvent*)event {
  [self handleMouseEvent:event
                    type:xe::ui::MacWindow::NativeMouseEvent::kWheel
                  button:xe::ui::MouseEvent::Button::kNone];
}

- (void)handleKeyEvent:(NSEvent*)event isUp:(BOOL)is_up {
  if (!xenia_window) {
    return;
  }
  NSEventModifierFlags modifiers = event.modifierFlags;
  uint32_t characters_first_unit = 0;
  if (!is_up) {
    NSString* characters = event.characters;
    if (characters.length) {
      characters_first_unit = [characters characterAtIndex:0];
    }
  }
  xenia_window->HandleKey(
      event.keyCode, characters_first_unit,
      (modifiers & NSEventModifierFlagShift) != 0,
      (modifiers & NSEventModifierFlagControl) != 0,
      (modifiers & NSEventModifierFlagOption) != 0,
      (modifiers & NSEventModifierFlagCommand) != 0, is_up ? true : false);
}

// Not calling super in keyDown / keyUp - the default NSView implementation
// forwards unhandled key events up the responder chain, ending in a beep.
- (void)keyDown:(NSEvent*)event {
  [self handleKeyEvent:event isUp:NO];
}

- (void)keyUp:(NSEvent*)event {
  [self handleKeyEvent:event isUp:YES];
}

// Modifier keys don't produce keyDown / keyUp - their transitions arrive here,
// with the direction recovered from whether the corresponding flag is set.
// (If both left and right of a pair are held and one is released, the flag
// stays set and the release is reported as another press - the
// device-independent flags can't distinguish sides.)
- (void)flagsChanged:(NSEvent*)event {
  if (!xenia_window) {
    return;
  }
  NSEventModifierFlags modifiers = event.modifierFlags;
  bool is_down;
  switch (event.keyCode) {
    case 0x38:  // kVK_Shift
    case 0x3C:  // kVK_RightShift
      is_down = (modifiers & NSEventModifierFlagShift) != 0;
      break;
    case 0x3B:  // kVK_Control
    case 0x3E:  // kVK_RightControl
      is_down = (modifiers & NSEventModifierFlagControl) != 0;
      break;
    case 0x3A:  // kVK_Option
    case 0x3D:  // kVK_RightOption
      is_down = (modifiers & NSEventModifierFlagOption) != 0;
      break;
    case 0x36:  // kVK_RightCommand
    case 0x37:  // kVK_Command
      is_down = (modifiers & NSEventModifierFlagCommand) != 0;
      break;
    default:
      // Caps Lock and Fn toggle rather than press/release cleanly.
      return;
  }
  xenia_window->HandleKey(event.keyCode, 0,
                          (modifiers & NSEventModifierFlagShift) != 0,
                          (modifiers & NSEventModifierFlagControl) != 0,
                          (modifiers & NSEventModifierFlagOption) != 0,
                          (modifiers & NSEventModifierFlagCommand) != 0,
                          !is_down);
}

@end

@interface XeMacWindowDelegate : NSObject <NSWindowDelegate> {
 @public
  xe::ui::MacWindow* xenia_window;
}
@end

@implementation XeMacWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  if (!xenia_window) {
    return YES;
  }
  return xenia_window->HandleWindowShouldClose() ? YES : NO;
}

- (void)windowDidResize:(NSNotification*)notification {
  if (xenia_window) {
    xenia_window->HandleSizeOrScaleUpdate();
  }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
  if (xenia_window) {
    xenia_window->HandleFocusUpdate(true);
  }
}

- (void)windowDidResignKey:(NSNotification*)notification {
  if (xenia_window) {
    xenia_window->HandleFocusUpdate(false);
  }
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
  if (xenia_window) {
    xenia_window->HandleFullscreenTransition(true);
  }
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
  if (xenia_window) {
    xenia_window->HandleFullscreenTransition(false);
  }
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
  if (xenia_window) {
    xenia_window->HandleSizeOrScaleUpdate();
  }
}

@end

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<MacWindow>(app_context, title, desired_logical_width,
                                     desired_logical_height);
}

MacWindow::MacWindow(WindowedAppContext& app_context,
                     const std::string_view title,
                     uint32_t desired_logical_width,
                     uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width,
             desired_logical_height) {}

MacWindow::~MacWindow() {
  EnterDestructor();
  DetachAndReleaseNative(true);
}

bool MacWindow::OpenImpl() {
  // The desired logical size is in medium-DPI (96) units, which is exactly
  // AppKit points - the backing scale factor handles Retina.
  NSRect content_rect = NSMakeRect(0.0, 0.0, double(GetDesiredLogicalWidth()),
                                   double(GetDesiredLogicalHeight()));
  NSUInteger style_mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable;
  window_ = [[NSWindow alloc] initWithContentRect:content_rect
                                        styleMask:style_mask
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  if (!window_) {
    return false;
  }
  // The window is owned by this MacWindow, not by AppKit's close handling.
  window_.releasedWhenClosed = NO;
  window_.title = [NSString stringWithUTF8String:GetTitle().c_str()];
  window_.acceptsMouseMovedEvents = YES;

  view_ = [[XeMacMetalView alloc] initWithFrame:content_rect];
  view_->xenia_window = this;
  view_.wantsLayer = YES;
  // With a custom backing layer, the redraw policy defaults to never - it must
  // be opted into for setNeedsDisplay to invoke updateLayer, which is what
  // drives painting.
  view_.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
  window_.contentView = view_;

  delegate_ = [[XeMacWindowDelegate alloc] init];
  delegate_->xenia_window = this;
  window_.delegate = delegate_;

  [window_ center];
  [window_ makeFirstResponder:view_];
  [window_ makeKeyAndOrderFront:nil];

  // After setting up the initial layout for non-fullscreen, enter fullscreen
  // if requested.
  if (IsFullscreen() &&
      !(window_.styleMask & NSWindowStyleMaskFullScreen)) {
    [window_ toggleFullScreen:nil];
  }

  UpdateLayerDrawableSize();
  latest_dpi_ =
      uint32_t(std::lround(GetMediumDpi() * window_.backingScaleFactor));

  // Make sure the initial state after opening is reported to the common
  // Window class no matter how AppKit sends the notifications.
  {
    WindowDestructionReceiver destruction_receiver(this);

    NSSize content_size = view_.bounds.size;
    CGFloat scale = window_.backingScaleFactor;
    OnActualSizeUpdate(uint32_t(std::lround(content_size.width * scale)),
                       uint32_t(std::lround(content_size.height * scale)),
                       destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return true;
    }

    if (window_.keyWindow) {
      OnFocusUpdate(true, destruction_receiver);
      if (destruction_receiver.IsWindowDestroyedOrClosed()) {
        return true;
      }
    }
  }

  return true;
}

void MacWindow::RequestCloseImpl() { [window_ performClose:nil]; }

uint32_t MacWindow::GetLatestDpiImpl() const { return latest_dpi_; }

void MacWindow::ApplyNewFullscreen() {
  bool is_natively_fullscreen =
      (window_.styleMask & NSWindowStyleMaskFullScreen) != 0;
  if (IsFullscreen() != is_natively_fullscreen) {
    [window_ toggleFullScreen:nil];
  }
}

void MacWindow::ApplyNewTitle() {
  window_.title = [NSString stringWithUTF8String:GetTitle().c_str()];
}

void MacWindow::FocusImpl() { [window_ makeKeyAndOrderFront:nil]; }

std::unique_ptr<Surface> MacWindow::CreateSurfaceImpl(
    Surface::TypeFlags allowed_types) {
  if (allowed_types & Surface::kTypeFlag_MacMetalLayer) {
    return std::make_unique<MacMetalLayerSurface>(
        static_cast<CAMetalLayer*>(view_.layer));
  }
  return nullptr;
}

void MacWindow::RequestPaintImpl() {
  // May be called from non-UI threads - view properties may only be touched in
  // the main thread. The block retains the view, so this is safe even if the
  // window is closed before the block is executed.
  // Not going through setNeedsDisplay / updateLayer - AppKit doesn't reliably
  // invoke the display cycle for views backed by a CAMetalLayer that is
  // presented to directly (by MoltenVK), so paint immediately in the UI
  // thread. The block retains the view, and the view's window pointer is
  // nulled when the MacWindow detaches, so this is safe even if the window is
  // closed or destroyed before the block is executed.
  XeMacMetalView* view = view_;
  dispatch_async(dispatch_get_main_queue(), ^{
    xe::ui::MacWindow* window = view->xenia_window;
    if (window) {
      window->HandlePaint();
    }
  });
}

void MacWindow::UpdateLayerDrawableSize() {
  CAMetalLayer* layer = static_cast<CAMetalLayer*>(view_.layer);
  if (!layer) {
    return;
  }
  CGFloat scale = window_.backingScaleFactor;
  if (scale <= 0.0) {
    scale = 1.0;
  }
  NSSize content_size = view_.bounds.size;
  layer.contentsScale = scale;
  layer.drawableSize = CGSizeMake(std::lround(content_size.width * scale),
                                  std::lround(content_size.height * scale));
}

void MacWindow::DetachAndReleaseNative(bool close_native_window) {
  if (!window_) {
    return;
  }
  NSWindow* window = window_;
  window_ = nullptr;
  view_->xenia_window = nullptr;
  delegate_->xenia_window = nullptr;
  window.delegate = nil;
  if (close_native_window) {
    [window close];
  }
  [view_ release];
  view_ = nullptr;
  [delegate_ release];
  delegate_ = nullptr;
  // Autorelease rather than release the window in case AppKit is still in the
  // middle of processing an event or a close involving it on the call stack.
  [window autorelease];
}

bool MacWindow::HandleWindowShouldClose() {
  WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    // The destructor has already detached and closed the native window - tell
    // AppKit not to touch it further from performClose.
    return false;
  }
  // AppKit itself will close the native window after the delegate returns.
  DetachAndReleaseNative(false);
  OnAfterClose();
  return true;
}

void MacWindow::HandleSizeOrScaleUpdate() {
  if (!window_) {
    return;
  }
  UpdateLayerDrawableSize();
  latest_dpi_ =
      uint32_t(std::lround(GetMediumDpi() * window_.backingScaleFactor));
  NSSize content_size = view_.bounds.size;
  if (!IsFullscreen() && !window_.zoomed) {
    OnDesiredLogicalSizeUpdate(uint32_t(std::lround(content_size.width)),
                               uint32_t(std::lround(content_size.height)));
  }
  CGFloat scale = window_.backingScaleFactor;
  WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(uint32_t(std::lround(content_size.width * scale)),
                     uint32_t(std::lround(content_size.height * scale)),
                     destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void MacWindow::HandleFocusUpdate(bool new_has_focus) {
  WindowDestructionReceiver destruction_receiver(this);
  OnFocusUpdate(new_has_focus, destruction_receiver);
  if (destruction_receiver.IsWindowDestroyedOrClosed()) {
    return;
  }
}

void MacWindow::HandleFullscreenTransition(bool new_fullscreen) {
  OnDesiredFullscreenUpdate(new_fullscreen);
}

void MacWindow::HandlePaint() { OnPaint(); }

void MacWindow::HandleMouse(NativeMouseEvent type, MouseEvent::Button button,
                            float x, float y, float scroll_x, float scroll_y) {
  CGFloat scale = window_.backingScaleFactor;
  MouseEvent e(this, button, int32_t(std::lround(x * scale)),
               int32_t(std::lround(y * scale)),
               int32_t(std::lround(scroll_x * MouseEvent::kScrollPerDetent)),
               int32_t(std::lround(scroll_y * MouseEvent::kScrollPerDetent)));
  WindowDestructionReceiver destruction_receiver(this);
  switch (type) {
    case NativeMouseEvent::kDown:
      OnMouseDown(e, destruction_receiver);
      break;
    case NativeMouseEvent::kUp:
      OnMouseUp(e, destruction_receiver);
      break;
    case NativeMouseEvent::kMove:
      OnMouseMove(e, destruction_receiver);
      break;
    case NativeMouseEvent::kWheel:
      OnMouseWheel(e, destruction_receiver);
      break;
  }
  // The window might have been destroyed by the handlers, don't interact with
  // *this from now on.
}

void MacWindow::HandleKey(uint16_t keycode, uint32_t characters_first_unit,
                          bool shift_pressed, bool ctrl_pressed,
                          bool alt_pressed, bool super_pressed, bool is_up) {
  VirtualKey virtual_key = TranslateKeycode(keycode);
  WindowDestructionReceiver destruction_receiver(this);
  if (virtual_key != VirtualKey::kNone) {
    KeyEvent e(this, virtual_key, 1, is_up, shift_pressed, ctrl_pressed,
               alt_pressed, super_pressed);
    if (is_up) {
      OnKeyUp(e, destruction_receiver);
    } else {
      OnKeyDown(e, destruction_receiver);
    }
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return;
    }
  }
  // Character input for text entry, excluding control characters and the
  // AppKit function-key range (arrows, F-keys and so on at U+F700-U+F8FF).
  // KeyEvent carries the character in the virtual key field for OnKeyChar,
  // like on other platforms.
  if (!is_up && characters_first_unit >= 0x20 && characters_first_unit != 0x7F &&
      !(characters_first_unit >= 0xF700 && characters_first_unit <= 0xF8FF)) {
    KeyEvent char_event(this, VirtualKey(characters_first_unit), 1, false,
                        shift_pressed, ctrl_pressed, alt_pressed,
                        super_pressed);
    OnKeyChar(char_event, destruction_receiver);
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return;
    }
  }
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::string& text,
                                               const std::string& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<MacMenuItem>(type, text, hotkey, std::move(callback));
}

}  // namespace ui
}  // namespace xe
