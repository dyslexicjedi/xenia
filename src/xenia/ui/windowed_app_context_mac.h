/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_MAC_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_MAC_H_

#include <CoreFoundation/CoreFoundation.h>

#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class MacWindowedAppContext final : public WindowedAppContext {
 public:
  // Must be created in the UI (main) thread, after the shared NSApplication
  // has been created.
  MacWindowedAppContext();
  ~MacWindowedAppContext();

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  void RunMainMacLoop();

 private:
  static void PendingFunctionsSourcePerform(void* info);

  // A manually-signaled run loop source on the main run loop, so pending
  // functions can be executed from within [NSApp run] (which is built on the
  // main CFRunLoop) when signaled from any thread.
  CFRunLoopRef main_run_loop_ = nullptr;
  CFRunLoopSourceRef pending_functions_source_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_MAC_H_
