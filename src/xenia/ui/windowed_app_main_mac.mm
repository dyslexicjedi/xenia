/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import <AppKit/AppKit.h>

#include <cstdlib>
#include <memory>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context_mac.h"

int main(int argc, char** argv) {
  int result;

  @autoreleasepool {
    // Create the shared application and make it a regular foreground app with
    // a Dock icon - Xenia executables are plain binaries, not .app bundles,
    // which would otherwise get the background (no windows) policy.
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    xe::ui::MacWindowedAppContext app_context;

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    cvar::ParseLaunchArguments(argc, argv, app->GetPositionalOptionsUsage(),
                               app->GetPositionalOptions());

    // Initialize logging. Needs parsed cvars.
    xe::InitializeLogging(app->GetName());

    if (app->OnInitialize()) {
      [NSApp activateIgnoringOtherApps:YES];
      app_context.RunMainMacLoop();
      result = EXIT_SUCCESS;
    } else {
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownLogging();

  return result;
}
