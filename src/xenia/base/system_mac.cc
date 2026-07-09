/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdlib>
#include <iostream>
#include <string>

#include "xenia/base/system.h"

namespace xe {

void LaunchWebBrowser(const std::string_view url) {
  std::string cmd("open \"");
  cmd.append(url);
  cmd.append("\"");
  system(cmd.c_str());
}

void LaunchFileExplorer(const std::filesystem::path& path) {
  std::string cmd("open -R \"");
  cmd.append(path.string());
  cmd.append("\"");
  system(cmd.c_str());
}

void ShowSimpleMessageBox(SimpleMessageBoxType type, std::string_view message) {
  // TODO(macos): show a native alert once the Cocoa UI layer exists.
  const char* prefix;
  switch (type) {
    default:
    case SimpleMessageBoxType::Help:
      prefix = "[Xenia Help] ";
      break;
    case SimpleMessageBoxType::Warning:
      prefix = "[Xenia Warning] ";
      break;
    case SimpleMessageBoxType::Error:
      prefix = "[Xenia Error] ";
      break;
  }
  std::cerr << prefix << message << std::endl;
}

}  // namespace xe
