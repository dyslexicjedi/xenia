/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_code_cache.h"

#include <cstdlib>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace xe::literals;

A64CodeCache::A64CodeCache() = default;

A64CodeCache::~A64CodeCache() {
  if (indirection_table_base_) {
    xe::memory::DeallocFixed(indirection_table_base_, 0,
                             xe::memory::DeallocationType::kRelease);
  }
  if (generated_code_base_) {
    xe::memory::DeallocFixed(generated_code_base_, 0,
                             xe::memory::DeallocationType::kRelease);
  }
}

std::unique_ptr<A64CodeCache> A64CodeCache::Create() {
  return std::unique_ptr<A64CodeCache>(new A64CodeCache());
}

bool A64CodeCache::Initialize() {
  // Dynamic-base VA reservations; committed as ranges get used.
  indirection_table_base_ = reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
      nullptr, kIndirectionTableSize, xe::memory::AllocationType::kReserve,
      xe::memory::PageAccess::kReadWrite));
  if (!indirection_table_base_) {
    XELOGE("Unable to allocate code cache indirection table");
  }

  // Allocate the whole generated code region as JIT memory up front: macOS
  // refuses mprotect() to rwx even on MAP_JIT mappings, so the permissions
  // must be set at mmap time. Physical pages are still only faulted in on
  // first touch. Writes require per-thread JIT write access.
  generated_code_base_ = reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
      nullptr, kGeneratedCodeSize, xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kExecuteReadWrite));
  if (!generated_code_base_) {
    XELOGE("Unable to allocate code cache generated code storage");
    return false;
  }

  // Preallocate the function map to a large, reasonable size.
  generated_code_map_.reserve(kMaximumFunctionCount);

  return true;
}

void A64CodeCache::set_indirection_default(uint64_t default_value) {
  indirection_default_value_ = default_value;
}

void A64CodeCache::AddIndirection(uint32_t guest_address,
                                  uint64_t host_address) {
  if (!indirection_table_base_) {
    return;
  }

  uint64_t* slot = reinterpret_cast<uint64_t*>(indirection_slot(guest_address));
  *slot = host_address;
}

void A64CodeCache::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  if (!indirection_table_base_) {
    return;
  }

  // Commit the memory.
  xe::memory::AllocFixed(
      reinterpret_cast<void*>(indirection_slot(guest_low)),
      (uint64_t(guest_high - guest_low) << 1),
      xe::memory::AllocationType::kCommit, xe::memory::PageAccess::kReadWrite);

  // Fill memory with the default value.
  uint64_t* p = reinterpret_cast<uint64_t*>(indirection_slot(guest_low));
  for (uint32_t address = guest_low; address < guest_high; address += 4) {
    p[(address - guest_low) / 4] = indirection_default_value_;
  }
}

void A64CodeCache::PlaceHostCode(uint32_t guest_address, void* machine_code,
                                 const EmitFunctionInfo& func_info,
                                 void*& code_execute_address_out,
                                 void*& code_write_address_out) {
  // Same for now. We may use different pools or whatnot later on, like when
  // we only want to place guest code in a serialized cache on disk.
  PlaceGuestCode(guest_address, machine_code, func_info, nullptr,
                 code_execute_address_out, code_write_address_out);
}

void A64CodeCache::PlaceGuestCode(uint32_t guest_address, void* machine_code,
                                  const EmitFunctionInfo& func_info,
                                  GuestFunction* function_info,
                                  void*& code_execute_address_out,
                                  void*& code_write_address_out) {
  // Hold a lock while we bump the pointers up.
  uint8_t* code_address;
  {
    auto global_lock = global_critical_region_.Acquire();

    // Reserve code. Always move the code to land on 16b alignment.
    code_address = generated_code_base_ + generated_code_offset_;
    code_execute_address_out = code_address;
    code_write_address_out = code_address;
    generated_code_offset_ += xe::round_up(func_info.code_size.total, 16);

    auto tail_address = generated_code_base_ + generated_code_offset_;


    // Store in map. It is maintained in sorted order of host PC dependent on
    // us also being append-only.
    generated_code_map_.emplace_back(
        (uint64_t(code_address - generated_code_base_) << 32) |
            generated_code_offset_,
        function_info);

    // The whole region is committed at Initialize (see above).

    // Copy code. The emitter relocates and flushes afterwards (via Emplace),
    // so no icache flush is needed here, but the copy itself needs JIT write
    // access.
    xe::memory::SetJitThreadWriteAccess(true);
    std::memcpy(code_address, machine_code, func_info.code_size.total);

    // Fill unused alignment slots with zeros (0x00000000 decodes as UDF #0,
    // which traps if ever reached).
    std::memset(code_address + func_info.code_size.total, 0,
                static_cast<size_t>(tail_address -
                                    (code_address + func_info.code_size.total)));
    xe::memory::SetJitThreadWriteAccess(false);
  }

  // The indirection table slot is NOT written here: the code copied above
  // still has pending label relocations (resolved by the emitter's ready()
  // after placement) and hasn't been instruction-cache flushed. Publishing the
  // slot now would let other guest threads branch into unrelocated / stale
  // code. A64Assembler::Assemble installs the indirection after the emitter
  // has relocated and flushed the code.
}

uintptr_t A64CodeCache::PlaceData(const void* data, size_t length) {
  // Hold a lock while we bump the pointers up.
  uint8_t* data_address = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();

    // Reserve. Always move the data to land on 16b alignment.
    data_address = generated_code_base_ + generated_code_offset_;
    generated_code_offset_ += xe::round_up(length, 16);

  }

  // The whole region is committed at Initialize (see above).

  // Copy data.
  xe::memory::SetJitThreadWriteAccess(true);
  std::memcpy(data_address, data, length);
  xe::memory::SetJitThreadWriteAccess(false);

  return uintptr_t(data_address);
}

GuestFunction* A64CodeCache::LookupFunction(uint64_t host_pc) {
  uint32_t key =
      uint32_t(host_pc - reinterpret_cast<uint64_t>(generated_code_base_));
  void* fn_entry = std::bsearch(
      &key, generated_code_map_.data(), generated_code_map_.size() + 1,
      sizeof(std::pair<uint32_t, Function*>),
      [](const void* key_ptr, const void* element_ptr) {
        auto key = *reinterpret_cast<const uint32_t*>(key_ptr);
        auto element =
            reinterpret_cast<const std::pair<uint64_t, GuestFunction*>*>(
                element_ptr);
        if (key < (element->first >> 32)) {
          return -1;
        } else if (key > uint32_t(element->first)) {
          return 1;
        } else {
          return 0;
        }
      });
  if (fn_entry) {
    return reinterpret_cast<const std::pair<uint64_t, GuestFunction*>*>(
               fn_entry)
        ->second;
  } else {
    return nullptr;
  }
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
