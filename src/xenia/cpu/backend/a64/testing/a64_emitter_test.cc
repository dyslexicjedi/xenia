/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdint>

#include "xenia/base/memory.h"
#include "xenia/base/platform.h"

#include "third_party/catch/include/catch.hpp"
#include "third_party/xbyak_aarch64/xbyak_aarch64/xbyak_aarch64.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {
namespace test {

// Stage 4.0 spike for the a64 backend: generate code with xbyak_aarch64
// directly into MAP_JIT memory under per-thread JIT write access, flush the
// instruction cache and execute — the same flow the a64 code cache will use.
TEST_CASE("emit_into_jit_memory_and_execute", "[a64_emitter]") {
  const size_t length = xe::memory::page_size();
  void* memory = xe::memory::AllocFixed(
      nullptr, length, xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kExecuteReadWrite);
  REQUIRE(memory != nullptr);

  xe::memory::SetJitThreadWriteAccess(true);
  // int add_42(int x) { return x + 42; }, with a forward branch over a brk
  // to exercise label resolution.
  Xbyak_aarch64::CodeGenerator gen(length, memory);
  Xbyak_aarch64::Label done;
  gen.add(gen.w0, gen.w0, 42);
  gen.b(done);
  gen.brk(0);  // Skipped by the branch; faults if label resolution is broken.
  gen.L(done);
  gen.ret();
  // Not ready(): it re-protects the buffer itself, which fights the MAP_JIT
  // W^X discipline. Label fixups happen at L() time for fixed-size buffers;
  // icache flushing is done below through xe::memory.
  REQUIRE(!gen.hasUndefinedLabel());
  xe::memory::SetJitThreadWriteAccess(false);
  xe::memory::FlushInstructionCache(memory, gen.getSize());

  auto add_42 = reinterpret_cast<int (*)(int)>(memory);
  REQUIRE(add_42(0) == 42);
  REQUIRE(add_42(58) == 100);
  REQUIRE(add_42(-42) == 0);

  xe::memory::DeallocFixed(memory, length,
                           xe::memory::DeallocationType::kRelease);
}

}  // namespace test
}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
