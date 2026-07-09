/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include "xenia/base/system.h"

extern char** environ;

namespace xe {

namespace {

// Runs /usr/bin/open with the given arguments, bypassing the shell so URLs
// and paths can't inject shell syntax.
void Open(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>("open"));
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  pid_t pid;
  if (posix_spawnp(&pid, "open", nullptr, nullptr, argv.data(), environ) ==
      0) {
    int status;
    waitpid(pid, &status, 0);
  }
}

}  // namespace

void LaunchWebBrowser(const std::string_view url) {
  Open({std::string(url)});
}

void LaunchFileExplorer(const std::filesystem::path& path) {
  // Reveal in Finder.
  Open({"-R", path.string()});
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
