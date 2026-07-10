/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_
#define XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/code_cache.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

struct EmitFunctionInfo {
  struct _code_size {
    size_t prolog;
    size_t body;
    size_t epilog;
    size_t tail;
    size_t total;
  } code_size;
  size_t prolog_stack_alloc_offset;  // offset of instruction after stack alloc
  size_t stack_size;
};

// ARM64 code cache. Differences from the x64 one:
// - macOS arm64 mandates a full 4 GB __PAGEZERO, so nothing can live below
//   4 GB: the indirection table and generated-code region are allocated at
//   dynamic addresses and indirection slots are 8 bytes (full host pointers),
//   addressed as table_base + (guest_address - kGuestCodeBase) * 2.
// - There is no separate write view: the generated-code region is a MAP_JIT
//   allocation where writability is toggled per thread
//   (pthread_jit_write_protect_np), so execute and write addresses are equal.
// - Unwind info is not registered (the posix x64 cache doesn't either).
class A64CodeCache : public CodeCache {
 public:
  ~A64CodeCache() override;

  static std::unique_ptr<A64CodeCache> Create();

  virtual bool Initialize();

  const std::filesystem::path& file_name() const override { return file_name_; }
  uintptr_t execute_base_address() const override {
    return reinterpret_cast<uintptr_t>(generated_code_base_);
  }
  size_t total_size() const override { return kGeneratedCodeSize; }

  bool has_indirection_table() { return indirection_table_base_ != nullptr; }
  void set_indirection_default(uint64_t default_value);
  void AddIndirection(uint32_t guest_address, uint64_t host_address);

  // Address of the 8-byte indirection slot for a guest address.
  uint64_t indirection_slot(uint32_t guest_address) const {
    return reinterpret_cast<uint64_t>(indirection_table_base_) +
           (uint64_t(guest_address - kGuestCodeBase) << 1);
  }
  // Constant to add to (guest_address << 1) to get the slot address.
  uint64_t indirection_slot_bias() const {
    return reinterpret_cast<uint64_t>(indirection_table_base_) -
           (uint64_t(kGuestCodeBase) << 1);
  }

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high);

  void PlaceHostCode(uint32_t guest_address, void* machine_code,
                     const EmitFunctionInfo& func_info,
                     void*& code_execute_address_out,
                     void*& code_write_address_out);
  void PlaceGuestCode(uint32_t guest_address, void* machine_code,
                      const EmitFunctionInfo& func_info,
                      GuestFunction* function_info,
                      void*& code_execute_address_out,
                      void*& code_write_address_out);
  uintptr_t PlaceData(const void* data, size_t length);

  GuestFunction* LookupFunction(uint64_t host_pc) override;

  // No unwind info is registered for JIT frames (matches the posix x64 code
  // cache); stack walkers just won't see through guest frames.
  void* LookupUnwindInfo(uint64_t host_pc) override { return nullptr; }

 protected:
  // All executable guest code falls within 0x80000000 to 0x9FFFFFFF.
  static const uint32_t kGuestCodeBase = 0x80000000;
  static const size_t kGuestCodeRange = 0x20000000;
  // 8-byte slots, one per possible (4-byte-aligned would suffice) address.
  static const size_t kIndirectionTableSize = kGuestCodeRange * 2;
  // The code range is 512MB, but we know the total code games will have is
  // pretty small (dozens of mb at most) and our expansion is reasonablish
  // so 256MB should be more than enough.
  static const size_t kGeneratedCodeSize = 0x0FFFFFFF;

  // This is picked to be high enough to cover whatever we can reasonably
  // expect. If we hit issues with this it probably means some corner case
  // in analysis triggering.
  static const size_t kMaximumFunctionCount = 100000;

  A64CodeCache();

  std::filesystem::path file_name_;

  // NOTE: the global critical region must be held when manipulating the
  // offsets or counts of anything, to keep the tables consistent and ordered.
  xe::global_critical_region global_critical_region_;

  // Value that the indirection table will be initialized with upon commit.
  uint64_t indirection_default_value_ = 0xFEEDF00DFEEDF00Dull;

  // 8-byte host code pointers, indexed by guest address (see
  // indirection_slot). Plain read/write memory. Dynamic base.
  uint8_t* indirection_table_base_ = nullptr;
  // Generated code arena. MAP_JIT: writes require
  // SetJitThreadWriteAccess(true). Dynamic base.
  uint8_t* generated_code_base_ = nullptr;
  // Current offset to empty space in generated code.
  size_t generated_code_offset_ = 0;
  // Current high water mark of COMMITTED code.
  std::atomic<size_t> generated_code_commit_mark_ = {0};
  // Sorted map by host PC base offsets to source function info.
  // This can be used to bsearch on host PC to find the guest function.
  // The key is [start offset | end offset] relative to the code base.
  std::vector<std::pair<uint64_t, GuestFunction*>> generated_code_map_;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_
