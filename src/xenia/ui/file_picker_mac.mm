/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/file_picker.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <filesystem>
#include <string>
#include <vector>

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

class MacFilePicker final : public FilePicker {
 public:
  bool Show(Window* parent_window) override;
};

std::unique_ptr<FilePicker> FilePicker::Create() { return std::make_unique<MacFilePicker>(); }

bool MacFilePicker::Show(Window* parent_window) {
  // Save dialogs are not implemented, matching the Win32 picker.
  assert_true(mode() == Mode::kOpen);

  NSOpenPanel* panel = [NSOpenPanel openPanel];
  [panel setTitle:[NSString stringWithUTF8String:title().c_str()]];
  [panel setAllowsMultipleSelection:multi_selection()];
  [panel setCanChooseFiles:type() == Type::kFile];
  [panel setCanChooseDirectories:type() == Type::kDirectory];

  if (type() == Type::kFile && !extensions().empty()) {
    NSMutableArray<UTType*>* allowed_types = [NSMutableArray array];
    for (const auto& extension : extensions()) {
      const std::string& pattern = extension.second;
      size_t start = 0;
      while (start < pattern.size()) {
        size_t end = pattern.find(';', start);
        std::string item = pattern.substr(start, end - start);
        if (item.rfind("*.", 0) == 0) {
          item.erase(0, 2);
        }
        if (!item.empty() && item != "*") {
          UTType* type = [UTType
              typeWithFilenameExtension:[NSString
                                            stringWithUTF8String:item.c_str()]];
          if (type) {
            [allowed_types addObject:type];
          }
        }
        if (end == std::string::npos) {
          break;
        }
        start = end + 1;
      }
    }
    if ([allowed_types count]) {
      [panel setAllowedContentTypes:allowed_types];
    }
  }

  // The picker is only called from windowed app initialization, on the main
  // thread. A sheet would need a completion callback and nested event-loop
  // coordination, while runModal has the synchronous FilePicker contract.
  if ([panel runModal] != NSModalResponseOK) {
    return false;
  }

  std::vector<std::filesystem::path> selected_files;
  for (NSURL* url in [panel URLs]) {
    const char* path = [[url path] UTF8String];
    if (path) {
      selected_files.emplace_back(path);
    }
  }
  if (selected_files.empty()) {
    return false;
  }
  set_selected_files(std::move(selected_files));
  return true;
}

}  // namespace ui
}  // namespace xe
