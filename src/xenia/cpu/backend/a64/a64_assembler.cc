/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_assembler.h"

#include <climits>
#include <cstring>

#include "third_party/capstone/include/capstone/arm64.h"
#include "third_party/capstone/include/capstone/capstone.h"
#include "xenia/base/profiling.h"
#include "xenia/base/reset_scope.h"
#include "xenia/base/string.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using xe::cpu::hir::HIRBuilder;

A64Assembler::A64Assembler(A64Backend* backend)
    : Assembler(backend), a64_backend_(backend) {
  if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &capstone_handle_) != CS_ERR_OK) {
    assert_always("Failed to initialize ARM64 capstone");
  }
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_OFF);
  cs_option(capstone_handle_, CS_OPT_SKIPDATA, CS_OPT_OFF);
}

A64Assembler::~A64Assembler() {
  // Emitter must be freed before the allocator.
  emitter_.reset();
  allocator_.reset();

  if (capstone_handle_) {
    cs_close(&capstone_handle_);
  }
}

bool A64Assembler::Initialize() {
  if (!Assembler::Initialize()) {
    return false;
  }

  allocator_.reset(new XbyakAllocator());
  emitter_.reset(new A64Emitter(a64_backend_, allocator_.get()));

  return true;
}

void A64Assembler::Reset() {
  string_buffer_.Reset();
  Assembler::Reset();
}

bool A64Assembler::Assemble(GuestFunction* function, HIRBuilder* builder,
                            uint32_t debug_info_flags,
                            std::unique_ptr<FunctionDebugInfo> debug_info) {
  SCOPE_profile_cpu_f("cpu");

  // Reset when we leave.
  xe::make_reset_scope(this);

  // Lower HIR -> a64.
  void* machine_code = nullptr;
  size_t code_size = 0;
  if (!emitter_->Emit(function, builder, debug_info_flags, debug_info.get(),
                      &machine_code, &code_size, &function->source_map())) {
    return false;
  }

  // Stash generated machine code.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmMachineCode) {
    DumpMachineCode(machine_code, code_size, function->source_map(),
                    &string_buffer_);
    debug_info->set_machine_code_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  function->set_debug_info(std::move(debug_info));
  static_cast<A64Function*>(function)->Setup(
      reinterpret_cast<uint8_t*>(machine_code), code_size);

  // Install into indirection table.
  reinterpret_cast<A64CodeCache*>(backend_->code_cache())
      ->AddIndirection(function->address(),
                       reinterpret_cast<uint64_t>(machine_code));

  return true;
}

void A64Assembler::DumpMachineCode(
    void* machine_code, size_t code_size,
    const std::vector<SourceMapEntry>& source_map, StringBuffer* str) {
  if (source_map.empty()) {
    return;
  }
  auto source_map_index = 0;
  uint32_t next_code_offset = source_map[0].code_offset;

  const auto code_base = reinterpret_cast<const uint8_t*>(machine_code);
  const uint8_t* code_ptr = code_base;
  size_t remaining_code_size = code_size;
  uint64_t address = reinterpret_cast<uint64_t>(machine_code);
  while (remaining_code_size) {
    auto code_offset = static_cast<uint32_t>(code_ptr - code_base);
    if (code_offset >= next_code_offset &&
        source_map_index < source_map.size()) {
      auto& source_map_entry = source_map[source_map_index];
      str->AppendFormat("{:08X} ", source_map_entry.guest_address);
      ++source_map_index;
      next_code_offset = source_map_index < source_map.size()
                             ? source_map[source_map_index].code_offset
                             : UINT_MAX;
    } else {
      str->Append("         ");
    }

    cs_insn insn = {};
    if (cs_disasm_iter(capstone_handle_, &code_ptr, &remaining_code_size,
                       &address, &insn)) {
      str->AppendFormat("{:016X}  {:<7} {}\n", insn.address, insn.mnemonic,
                        insn.op_str);
    } else {
      // ARM64 instructions are always four bytes. Keep dumps useful for
      // permanently undefined encodings (including our breakpoint marker)
      // that Capstone deliberately refuses to decode.
      if (remaining_code_size < sizeof(uint32_t)) {
        break;
      }
      uint32_t word;
      std::memcpy(&word, code_ptr, sizeof(word));
      str->AppendFormat("{:016X}  .word   {:08X}\n", address, word);
      code_ptr += sizeof(word);
      remaining_code_size -= sizeof(word);
      address += sizeof(word);
    }
  }
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
