/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <filesystem>

#include "xenia/base/filesystem.h"

#include "third_party/catch/include/catch.hpp"

namespace xe {
namespace base {
namespace test {

TEST_CASE("executable_path", "[filesystem]") {
  auto path = filesystem::GetExecutablePath();
  REQUIRE(!path.empty());
  REQUIRE(std::filesystem::exists(path));

  auto folder = filesystem::GetExecutableFolder();
  REQUIRE(!folder.empty());
  REQUIRE(std::filesystem::is_directory(folder));
}

TEST_CASE("user_folder", "[filesystem]") {
  auto folder = filesystem::GetUserFolder();
  REQUIRE(!folder.empty());
}

}  // namespace test
}  // namespace base
}  // namespace xe
