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

#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/thread_debug_info.h"

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

TEST_CASE("calculate_next_host_instruction", "[a64_debugger]") {
  alignas(16) uint32_t code[16] = {};
  Xbyak_aarch64::CodeGenerator gen(sizeof(code), code);
  Xbyak_aarch64::Label target;

  gen.b(Xbyak_aarch64::EQ, target);  // 0
  gen.cbz(gen.w0, target);           // 4
  gen.tbnz(gen.x1, 40, target);      // 8
  gen.br(gen.x2);                    // 12
  gen.blr(gen.x3);                   // 16
  gen.ret();                         // 20
  gen.bl(target);                    // 24
  gen.nop();                         // 28
  gen.L(target);                     // 32
  gen.nop();
  REQUIRE(!gen.hasUndefinedLabel());

  A64Backend backend;
  ThreadDebugInfo thread_info = {};
  const auto base = reinterpret_cast<uint64_t>(code);
  const auto next = [&backend, &thread_info, base](size_t offset) {
    return backend.CalculateNextHostInstruction(&thread_info, base + offset);
  };

  // B.cond follows NZCV from the captured PSTATE.
  thread_info.host_context.pstate = UINT64_C(1) << 30;  // Z
  REQUIRE(next(0) == base + 32);
  thread_info.host_context.pstate = 0;
  REQUIRE(next(0) == base + 4);

  // W-register compare ignores the high half of X0.
  thread_info.host_context.x[0] = UINT64_C(0x1234000000000000);
  REQUIRE(next(4) == base + 32);
  thread_info.host_context.x[0] |= 1;
  REQUIRE(next(4) == base + 8);

  thread_info.host_context.x[1] = UINT64_C(1) << 40;
  REQUIRE(next(8) == base + 32);
  thread_info.host_context.x[1] = 0;
  REQUIRE(next(8) == base + 12);

  thread_info.host_context.x[2] = UINT64_C(0x123456789ABCDEF0);
  REQUIRE(next(12) == thread_info.host_context.x[2]);
  thread_info.host_context.x[3] = UINT64_C(0x0FEDCBA987654320);
  REQUIRE(next(16) == thread_info.host_context.x[3]);
  thread_info.host_context.x[30] = UINT64_C(0x1122334455667788);
  REQUIRE(next(20) == thread_info.host_context.x[30]);
  REQUIRE(next(24) == base + 32);
  REQUIRE(next(28) == base + 32);
}

TEST_CASE("install_and_uninstall_breakpoint", "[a64_debugger]") {
  const size_t length = xe::memory::page_size();
  void* memory = xe::memory::AllocFixed(
      nullptr, length, xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kExecuteReadWrite);
  REQUIRE(memory != nullptr);

  constexpr uint32_t kNop = UINT32_C(0xD503201F);
  xe::memory::SetJitThreadWriteAccess(true);
  std::memcpy(memory, &kNop, sizeof(kNop));
  xe::memory::SetJitThreadWriteAccess(false);
  xe::memory::FlushInstructionCache(memory, sizeof(kNop));

  A64Backend backend;
  Breakpoint breakpoint(
      nullptr, Breakpoint::AddressType::kHost,
      reinterpret_cast<uint64_t>(memory),
      [](Breakpoint*, ThreadDebugInfo*, uint64_t) {});
  backend.InstallBreakpoint(&breakpoint);

  uint32_t instruction;
  std::memcpy(&instruction, memory, sizeof(instruction));
  REQUIRE(instruction == UINT32_C(0x001BD5A0));  // UDF #0xDEAD.
  REQUIRE(breakpoint.backend_data().size() == 1);
  REQUIRE(breakpoint.backend_data()[0].second == kNop);

  backend.UninstallBreakpoint(&breakpoint);
  std::memcpy(&instruction, memory, sizeof(instruction));
  REQUIRE(instruction == kNop);
  REQUIRE(breakpoint.backend_data().empty());

  xe::memory::DeallocFixed(memory, length,
                           xe::memory::DeallocationType::kRelease);
}

}  // namespace test
}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
