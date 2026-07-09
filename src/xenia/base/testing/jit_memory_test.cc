/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdint>
#include <cstring>

#include "xenia/base/exception_handler.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"

#include "third_party/catch/include/catch.hpp"

namespace xe {
namespace base {
namespace test {

// Generated-code path used by the CPU backend code cache: allocate executable
// memory, write host machine code into it with JIT write access held, flush
// the instruction cache and execute the result.
TEST_CASE("jit_write_execute", "[jit_memory]") {
  const size_t length = xe::memory::page_size();
  void* memory = xe::memory::AllocFixed(
      nullptr, length, xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kExecuteReadWrite);
  REQUIRE(memory != nullptr);

  // int add_42(int x) { return x + 42; }
#if XE_ARCH_ARM64
  const uint32_t code[] = {
      0x1100A800,  // add w0, w0, #42
      0xD65F03C0,  // ret
  };
#elif XE_ARCH_AMD64
#if XE_PLATFORM_WIN32
  const uint8_t code[] = {
      0x8D, 0x41, 0x2A,  // lea eax, [rcx + 42]
      0xC3,              // ret
  };
#else
  const uint8_t code[] = {
      0x8D, 0x47, 0x2A,  // lea eax, [rdi + 42]
      0xC3,              // ret
  };
#endif
#else
#error Unsupported test architecture
#endif

  xe::memory::SetJitThreadWriteAccess(true);
  std::memcpy(memory, code, sizeof(code));
  xe::memory::SetJitThreadWriteAccess(false);
  xe::memory::FlushInstructionCache(memory, sizeof(code));

  auto add_42 = reinterpret_cast<int (*)(int)>(memory);
  REQUIRE(add_42(0) == 42);
  REQUIRE(add_42(58) == 100);
  REQUIRE(add_42(-42) == 0);

  xe::memory::DeallocFixed(memory, length,
                           xe::memory::DeallocationType::kRelease);
}

struct PageFaultCapture {
  void* page = nullptr;
  size_t length = 0;
  bool handled = false;
  uint64_t fault_address = 0;
  Exception::AccessViolationOperation operation =
      Exception::AccessViolationOperation::kUnknown;

  static bool Handler(Exception* ex, void* data) {
    auto capture = reinterpret_cast<PageFaultCapture*>(data);
    if (ex->code() != Exception::Code::kAccessViolation) {
      return false;
    }
    uint64_t page_begin = reinterpret_cast<uint64_t>(capture->page);
    if (ex->fault_address() < page_begin ||
        ex->fault_address() >= page_begin + capture->length) {
      return false;
    }
    capture->handled = true;
    capture->fault_address = ex->fault_address();
    capture->operation = ex->access_violation_operation();
    // Make the page accessible and retry the faulting instruction.
    xe::memory::Protect(capture->page, capture->length,
                        xe::memory::PageAccess::kReadWrite);
    return true;
  }
};

// Access-violation path used by MMIO and write watches: fault on a protected
// page, classify the access in the handler, fix it up and resume.
TEST_CASE("exception_handler_page_fault", "[jit_memory]") {
  PageFaultCapture capture;
  capture.length = xe::memory::page_size();
  capture.page = xe::memory::AllocFixed(
      nullptr, capture.length, xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kReadWrite);
  REQUIRE(capture.page != nullptr);
  auto p_value = reinterpret_cast<volatile uint32_t*>(capture.page);
  *p_value = 0xC0DE;

  ExceptionHandler::Install(PageFaultCapture::Handler, &capture);

  SECTION("read classification") {
    REQUIRE(xe::memory::Protect(capture.page, capture.length,
                                xe::memory::PageAccess::kNoAccess));
    uint32_t value = *p_value;
    REQUIRE(capture.handled);
    REQUIRE(value == 0xC0DE);
    REQUIRE(capture.fault_address == reinterpret_cast<uint64_t>(capture.page));
    REQUIRE(capture.operation ==
            Exception::AccessViolationOperation::kRead);
  }

  SECTION("write classification") {
    REQUIRE(xe::memory::Protect(capture.page, capture.length,
                                xe::memory::PageAccess::kNoAccess));
    *p_value = 0xFEED;
    REQUIRE(capture.handled);
    REQUIRE(*p_value == 0xFEED);
    REQUIRE(capture.fault_address == reinterpret_cast<uint64_t>(capture.page));
    REQUIRE(capture.operation ==
            Exception::AccessViolationOperation::kWrite);
  }

  ExceptionHandler::Uninstall(PageFaultCapture::Handler, &capture);
  xe::memory::DeallocFixed(capture.page, capture.length,
                           xe::memory::DeallocationType::kRelease);
}

}  // namespace test
}  // namespace base
}  // namespace xe
