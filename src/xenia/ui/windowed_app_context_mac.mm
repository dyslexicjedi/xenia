/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_mac.h"

#import <AppKit/AppKit.h>

namespace xe {
namespace ui {

MacWindowedAppContext::MacWindowedAppContext() {
  main_run_loop_ = CFRunLoopGetMain();
  CFRetain(main_run_loop_);
  CFRunLoopSourceContext source_context = {};
  source_context.info = this;
  source_context.perform = PendingFunctionsSourcePerform;
  pending_functions_source_ = CFRunLoopSourceCreate(
      kCFAllocatorDefault, /* order */ 0, &source_context);
  CFRunLoopAddSource(main_run_loop_, pending_functions_source_,
                     kCFRunLoopCommonModes);
}

MacWindowedAppContext::~MacWindowedAppContext() {
  // Invalidate the source as its info pointer (to this context) is now
  // outdated. Invalidation is thread-safe and stops future perform callbacks.
  CFRunLoopSourceInvalidate(pending_functions_source_);
  CFRelease(pending_functions_source_);
  CFRelease(main_run_loop_);
}

void MacWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  CFRunLoopSourceSignal(pending_functions_source_);
  CFRunLoopWakeUp(main_run_loop_);
}

void MacWindowedAppContext::PlatformQuitFromUIThread() {
  [NSApp stop:nil];
  // -stop: only takes effect after the current event finishes processing, and
  // the loop may currently be blocked waiting for events - post a dummy event
  // so the loop wakes up and observes the stop request even if the quit was
  // initiated outside event handling (such as from a pending function).
  NSEvent* wake_event =
      [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                         location:NSMakePoint(0, 0)
                    modifierFlags:0
                        timestamp:0
                     windowNumber:0
                          context:nil
                          subtype:0
                            data1:0
                            data2:0];
  [NSApp postEvent:wake_event atStart:YES];
}

void MacWindowedAppContext::RunMainMacLoop() {
  // For safety, in case the quit request somehow happened before the loop.
  if (HasQuitFromUIThread()) {
    return;
  }
  [NSApp run];
  // Something else - not QuitFromUIThread - might have stopped the
  // application. If it has exited for some reason, let the context know, so
  // pending functions won't be added pointlessly.
  QuitFromUIThread();
}

void MacWindowedAppContext::PendingFunctionsSourcePerform(void* info) {
  auto app_context = static_cast<MacWindowedAppContext*>(info);
  app_context->ExecutePendingFunctionsFromUIThread();
}

}  // namespace ui
}  // namespace xe
