/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_backend.h"

#include <stddef.h>

#include <cstring>

#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/a64/a64_assembler.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace Xbyak_aarch64;

class A64ThunkEmitter : public A64Emitter {
 public:
  A64ThunkEmitter(A64Backend* backend, XbyakAllocator* allocator);
  ~A64ThunkEmitter() override;
  HostToGuestThunk EmitHostToGuestThunk();
  GuestToHostThunk EmitGuestToHostThunk();
  ResolveFunctionThunk EmitResolveFunctionThunk();

 private:
  // The following functions provide save/load functionality for registers.
  // They assume at least StackLayout::THUNK_STACK_SIZE bytes have been
  // allocated on the stack.
  void EmitSaveVolatileRegs();
  void EmitLoadVolatileRegs();
  void EmitSaveNonvolatileRegs();
  void EmitLoadNonvolatileRegs();
};

A64Backend::A64Backend() : Backend() {}

A64Backend::~A64Backend() {
  A64Emitter::FreeConstData(emitter_data_);
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
}

bool A64Backend::Initialize(Processor* processor) {
  if (!Backend::Initialize(processor)) {
    return false;
  }

  // MOVBE-style byte-swapped load/store fusion is not implemented yet.
  machine_info_.supports_extended_load_store = false;

  auto& gprs = machine_info_.register_sets[0];
  gprs.id = 0;
  std::strcpy(gprs.name, "gpr");
  gprs.types = MachineInfo::RegisterSet::INT_TYPES;
  gprs.count = A64Emitter::GPR_COUNT;

  auto& fprs = machine_info_.register_sets[1];
  fprs.id = 1;
  std::strcpy(fprs.name, "v");
  fprs.types = MachineInfo::RegisterSet::FLOAT_TYPES |
               MachineInfo::RegisterSet::VEC_TYPES;
  fprs.count = A64Emitter::FPR_COUNT;

  code_cache_ = A64CodeCache::Create();
  Backend::code_cache_ = code_cache_.get();
  if (!code_cache_->Initialize()) {
    return false;
  }

  // Generate thunks used to transition between jitted code and host code.
  XbyakAllocator allocator;
  A64ThunkEmitter thunk_emitter(this, &allocator);
  host_to_guest_thunk_ = thunk_emitter.EmitHostToGuestThunk();
  guest_to_host_thunk_ = thunk_emitter.EmitGuestToHostThunk();
  resolve_function_thunk_ = thunk_emitter.EmitResolveFunctionThunk();

  // Set the code cache to use the ResolveFunction thunk for default
  // indirections.
  code_cache_->set_indirection_default(uint64_t(resolve_function_thunk_));

  // Allocate some special indirections.
  code_cache_->CommitExecutableRange(0x9FFF0000, 0x9FFFFFFF);

  // Allocate emitter constant data.
  emitter_data_ = A64Emitter::PlaceConstData();

  // Setup exception callback
  ExceptionHandler::Install(&ExceptionCallbackThunk, this);

  return true;
}

void A64Backend::CommitExecutableRange(uint32_t guest_low,
                                       uint32_t guest_high) {
  code_cache_->CommitExecutableRange(guest_low, guest_high);
}

std::unique_ptr<Assembler> A64Backend::CreateAssembler() {
  return std::make_unique<A64Assembler>(this);
}

std::unique_ptr<GuestFunction> A64Backend::CreateGuestFunction(
    Module* module, uint32_t address) {
  return std::make_unique<A64Function>(module, address);
}

uint64_t A64Backend::CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                  uint64_t current_pc) {
  // TODO(macos): decode branches (B/BL/BR/BLR/RET/CBZ/CBNZ/TBZ/TBNZ/B.cond)
  // for debugger single-step. All ARM64 instructions are 4 bytes, so this is
  // correct for straight-line code.
  return current_pc + 4;
}

bool A64Backend::ExceptionCallbackThunk(Exception* ex, void* data) {
  auto backend = reinterpret_cast<A64Backend*>(data);
  return backend->ExceptionCallback(ex);
}

bool A64Backend::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kIllegalInstruction) {
    // We only care about illegal instructions. Other things will be handled by
    // other handlers (probably). If nothing else picks it up we'll be called
    // with OnUnhandledException to do real crash handling.
    return false;
  }

  // TODO(macos): breakpoint support (BRK #0 delivers SIGTRAP on Darwin, which
  // the exception handler doesn't route yet).
  return false;
}

A64ThunkEmitter::A64ThunkEmitter(A64Backend* backend, XbyakAllocator* allocator)
    : A64Emitter(backend, allocator) {}

A64ThunkEmitter::~A64ThunkEmitter() {}

HostToGuestThunk A64ThunkEmitter::EmitHostToGuestThunk() {
  // x0 = target
  // x1 = arg0 (context)
  // x2 = arg1 (guest return address)

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = getSize();

  stp(x29, x30, pre_ptr(sp, -16));
  mov(x29, sp);
  sub(sp, sp, (uint32_t)stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  // Save nonvolatile registers.
  EmitSaveNonvolatileRegs();

  mov(x16, x0);  // target
  mov(x27, x1);  // context
  ldr(x28, ptr(x27, (int32_t)offsetof(ppc::PPCContext, virtual_membase)));
  mov(x1, x2);   // guest return address (guest entry convention)
  blr(x16);

  EmitLoadNonvolatileRegs();

  code_offsets.epilog = getSize();

  add(sp, sp, (uint32_t)stack_size);
  ldp(x29, x30, post_ptr(sp, 16));
  ret();

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (HostToGuestThunk)fn;
}

GuestToHostThunk A64ThunkEmitter::EmitGuestToHostThunk() {
  // x16 = target function
  // x1  = arg0
  // x2  = arg1
  // x3  = arg2

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = getSize();

  stp(x29, x30, pre_ptr(sp, -16));
  mov(x29, sp);
  sub(sp, sp, (uint32_t)stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  // Save off volatile registers.
  EmitSaveVolatileRegs();

  mov(x0, x27);  // context
  blr(x16);

  EmitLoadVolatileRegs();

  code_offsets.epilog = getSize();

  add(sp, sp, (uint32_t)stack_size);
  ldp(x29, x30, post_ptr(sp, 16));
  ret();

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (GuestToHostThunk)fn;
}

// A64Emitter handles actually resolving functions.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

ResolveFunctionThunk A64ThunkEmitter::EmitResolveFunctionThunk() {
  // Entered via `br` from an unresolved indirection slot.
  // w17 = target PPC address (set by Call/CallIndirect)
  // x1  = guest call return address (must be preserved for the callee)
  // x27 = context

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = getSize();

  stp(x29, x30, pre_ptr(sp, -16));
  mov(x29, sp);
  sub(sp, sp, (uint32_t)stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  // Save volatile registers (including x1, which holds the guest return
  // address the resolved function expects).
  EmitSaveVolatileRegs();

  mov(x0, x27);  // context
  mov(w1, w17);  // target guest address
  MovConst(x16, reinterpret_cast<uint64_t>(&ResolveFunction));
  blr(x16);
  mov(x16, x0);  // resolved host code address

  EmitLoadVolatileRegs();

  code_offsets.epilog = getSize();

  add(sp, sp, (uint32_t)stack_size);
  ldp(x29, x30, post_ptr(sp, 16));
  br(x16);

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (ResolveFunctionThunk)fn;
}

void A64ThunkEmitter::EmitSaveVolatileRegs() {
  // x1 holds the guest return address at guest-to-host transition points;
  // the HIR vector pool (v16-v31) is caller-saved and may hold live values.
  str(x1, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[0])));
  stp(q16, q17, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[0])));
  stp(q18, q19, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[2])));
  stp(q20, q21, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[4])));
  stp(q22, q23, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[6])));
  stp(q24, q25, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[8])));
  stp(q26, q27, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[10])));
  stp(q28, q29, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[12])));
  stp(q30, q31, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[14])));
}

void A64ThunkEmitter::EmitLoadVolatileRegs() {
  ldr(x1, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[0])));
  ldp(q16, q17, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[0])));
  ldp(q18, q19, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[2])));
  ldp(q20, q21, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[4])));
  ldp(q22, q23, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[6])));
  ldp(q24, q25, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[8])));
  ldp(q26, q27, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[10])));
  ldp(q28, q29, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[12])));
  ldp(q30, q31, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[14])));
}

void A64ThunkEmitter::EmitSaveNonvolatileRegs() {
  stp(x19, x20, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[0])));
  stp(x21, x22, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[2])));
  stp(x23, x24, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[4])));
  stp(x25, x26, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[6])));
  stp(x27, x28, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[8])));
  stp(q8, q9, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[0])));
  stp(q10, q11, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[2])));
  stp(q12, q13, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[4])));
  stp(q14, q15, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[6])));
}

void A64ThunkEmitter::EmitLoadNonvolatileRegs() {
  ldp(x19, x20, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[0])));
  ldp(x21, x22, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[2])));
  ldp(x23, x24, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[4])));
  ldp(x25, x26, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[6])));
  ldp(x27, x28, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, r[8])));
  ldp(q8, q9, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[0])));
  ldp(q10, q11, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[2])));
  ldp(q12, q13, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[4])));
  ldp(q14, q15, ptr(sp, (int32_t)offsetof(StackLayout::Thunk, v[6])));
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
