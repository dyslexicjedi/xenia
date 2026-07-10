/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_emitter.h"

#include <stddef.h>

#include <climits>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/debugging.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_debug_info.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using namespace xe::literals;
using namespace Xbyak_aarch64;

static const size_t kMaxCodeSize = 1_MiB;

// HIR GPR pool: callee-saved so values survive guest-to-host transitions.
const uint32_t A64Emitter::gpr_reg_map_[A64Emitter::GPR_COUNT] = {
    19, 20, 21, 22, 23, 24, 25, 26,
};

// HIR FP/vector pool: caller-saved; the guest-to-host thunks save these.
const uint32_t A64Emitter::fpr_reg_map_[A64Emitter::FPR_COUNT] = {
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};

A64Emitter::A64Emitter(A64Backend* backend, XbyakAllocator* allocator)
    : CodeGenerator(kMaxCodeSize, Xbyak_aarch64::AutoGrow, allocator),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator) {}

A64Emitter::~A64Emitter() = default;

bool A64Emitter::Emit(GuestFunction* function, HIRBuilder* builder,
                      uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
                      void** out_code_address, size_t* out_code_size,
                      std::vector<SourceMapEntry>* out_source_map) {
  SCOPE_profile_cpu_f("cpu");

  // Reset.
  debug_info_ = debug_info;
  debug_info_flags_ = debug_info_flags;
  trace_data_ = &function->trace_data();
  source_map_arena_.Reset();

  // Fill the generator with code.
  EmitFunctionInfo func_info = {};
  if (!Emit(builder, func_info)) {
    return false;
  }

  // Copy the final code to the cache and relocate it.
  *out_code_size = getSize();
  *out_code_address = Emplace(func_info, function);

  // Stash source map.
  source_map_arena_.CloneContents(out_source_map);

  return true;
}

void* A64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  // The code cache is a single MAP_JIT mapping, so the write and execute
  // addresses are equal and label math is done against the final address.
  uint32_t* old_address = top_;
  void* new_execute_address;
  void* new_write_address;
  assert_true(func_info.code_size.total == getSize());
  if (function) {
    code_cache_->PlaceGuestCode(function->address(), top_, func_info, function,
                                new_execute_address, new_write_address);
  } else {
    code_cache_->PlaceHostCode(0, top_, func_info, new_execute_address,
                               new_write_address);
  }
  xe::memory::SetJitThreadWriteAccess(true);
  top_ = reinterpret_cast<uint32_t*>(new_write_address);
  ready();  // Resolves AutoGrow label relocations against the new address.
  xe::memory::SetJitThreadWriteAccess(false);
  xe::memory::FlushInstructionCache(new_execute_address,
                                    func_info.code_size.total);
  top_ = old_address;
  reset();
  hir_labels_.clear();
  return new_execute_address;
}

bool A64Emitter::Emit(HIRBuilder* builder, EmitFunctionInfo& func_info) {
  Xbyak_aarch64::Label epilog_label;
  epilog_label_ = &epilog_label;

  // Calculate stack size. We need to align things to their natural sizes.
  // This could be much better (sort by type/etc).
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = GetTypeSize(slot->type);

    // Align to natural size.
    stack_offset = xe::align(stack_offset, type_size);
    slot->set_constant((uint32_t)stack_offset);
    stack_offset += type_size;
  }

  // Ensure 16b alignment.
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = xe::align(stack_offset, static_cast<size_t>(16));

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = getSize();

  // Function prolog.
  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  assert_true(stack_size % 16 == 0);
  func_info.stack_size = stack_size;
  stack_size_ = stack_size;

  stp(x29, x30, pre_ptr(sp, -16));
  mov(x29, sp);
  sub(sp, sp, (uint32_t)stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  str(GetContextReg(), ptr(sp, (int32_t)StackLayout::GUEST_CTX_HOME));
  str(x1, ptr(sp, (int32_t)StackLayout::GUEST_RET_ADDR));
  str(xzr, ptr(sp, (int32_t)StackLayout::GUEST_CALL_RET_ADDR));

  // Safe now to do some tracing.
  if ((debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctions) &&
      trace_data_ && trace_data_->header()) {
    auto trace_header = trace_data_->header();

    // Call count (atomic add via LSE).
    MovConst(x0, uint64_t(&trace_header->function_call_count));
    MovConst(x1, 1);
    ldaddal(x1, xzr, ptr(x0));

    // TODO(macos): caller-history slots and function_thread_use bitmask.
  }

  // Load membase.
  ldr(GetMembaseReg(),
      ptr(GetContextReg(),
          (int32_t)offsetof(ppc::PPCContext, virtual_membase)));

  // Body.
  auto block = builder->first_block();
  while (block) {
    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      L(hir_label(label->name));
      label = label->next;
    }

    // Process instructions.
    const Instr* instr = block->instr_head;
    while (instr) {
      const Instr* new_tail = instr;
      if (!SelectSequence(this, instr, &new_tail)) {
        // No sequence found!
        // NOTE: If you encounter this after adding a new instruction, do a
        // full rebuild!
        // stderr directly: the async logger loses this line when the assert
        // below aborts the process.
        fprintf(stderr, "Unable to process HIR opcode %s\n",
                instr->opcode->name);
        XELOGE("Unable to process HIR opcode {}", instr->opcode->name);
        assert_always();
        break;
      }
      instr = new_tail;
    }

    block = block->next;
  }

  // Function epilog.
  L(epilog_label);
  epilog_label_ = nullptr;
  ldr(GetContextReg(), ptr(sp, (int32_t)StackLayout::GUEST_CTX_HOME));

  code_offsets.epilog = getSize();

  add(sp, sp, (uint32_t)stack_size);
  ldp(x29, x30, post_ptr(sp, 16));
  ret();

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;

  return true;
}

Xbyak_aarch64::Label& A64Emitter::hir_label(const char* name) {
  return hir_labels_[name];
}

void A64Emitter::MarkSourceOffset(const Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize());
}

void A64Emitter::DebugBreak() { brk(0); }

uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  uint32_t str_ptr = uint32_t(thread_state->context()->r[3]);
  auto str = thread_state->memory()->TranslateVirtual<const char*>(str_ptr);
  XELOGD("(DebugPrint) {}", str);
  return 0;
}

uint64_t TrapDebugBreak(void* raw_context, uint64_t address) {
  XELOGE("tw/td forced trap hit! This should be a crash!");
  if (cvars::break_on_debugbreak) {
    xe::debugging::Break();
  }
  return 0;
}

void A64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26:
      // 0x0FE00014 is a 'debug print' where r3 = buffer r4 = length
      CallNative(TrapDebugPrint, 0);
      break;
    case 0:
    case 22:
      // Always trap?
      // TODO(benvanik): post software interrupt to debugger.
      CallNative(TrapDebugBreak, 0);
      break;
    case 25:
      // ?
      break;
    default:
      XELOGW("Unknown trap type {}", trap_type);
      brk(0);
      break;
  }
}

void A64Emitter::UnimplementedInstr(const hir::Instr* i) {
  // TODO(benvanik): notify debugger.
  brk(0);
  assert_always();
}

// This is used by the A64ThunkEmitter's ResolveFunctionThunk.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);

  assert_not_zero(target_address);

  auto fn = thread_state->processor()->ResolveFunction(
      static_cast<uint32_t>(target_address));
  assert_not_null(fn);
  auto a64_fn = static_cast<A64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(a64_fn->machine_code());

  return addr;
}

void A64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  auto fn = static_cast<A64Function*>(function);
  // Resolve address to the function to call and store in x16.
  if (fn->machine_code()) {
    MovConst(x16, uint64_t(fn->machine_code()));
  } else if (code_cache_->has_indirection_table()) {
    // The slot holds either the address of the generated code or the resolve
    // thunk. w17 must hold the guest address when branching into the slot
    // target (the resolve thunk reads it).
    MovConst(w17, function->address());
    MovConst(x16, code_cache_->indirection_slot_bias());
    add(x16, x16, x17, LSL, 1);
    ldr(x16, ptr(x16));
  } else {
    // Old-style resolve. The result lands in x0.
    CallNative(&ResolveFunction, function->address());
    mov(x16, x0);
  }

  // Actually jump/call to x16.
  if (instr->flags & hir::CALL_TAIL) {
    // Pass the callers return address over, then tear our frame down and
    // branch.
    ldr(x1, ptr(sp, (int32_t)StackLayout::GUEST_RET_ADDR));
    add(sp, sp, static_cast<uint32_t>(stack_size()));
    ldp(x29, x30, post_ptr(sp, 16));
    br(x16);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    ldr(x1, ptr(sp, (int32_t)StackLayout::GUEST_CALL_RET_ADDR));
    blr(x16);
  }
}

void A64Emitter::CallIndirect(const hir::Instr* instr,
                              const WReg& reg) {
  // Check if return.
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    ldr(w16, ptr(sp, (int32_t)StackLayout::GUEST_RET_ADDR));
    cmp(reg, w16);
    beq(epilog_label());
  }

  // Load the code address from the indirection table:
  // slot = table_bias + (guest_address << 1); see A64CodeCache.
  if (code_cache_->has_indirection_table()) {
    mov(w17, reg);  // Zero-extends the guest address (resolve thunk arg).
    MovConst(x16, code_cache_->indirection_slot_bias());
    add(x16, x16, x17, LSL, 1);
    ldr(x16, ptr(x16));
  } else {
    // Old-style resolve.
    mov(w1, reg);
    CallNativeSafe(reinterpret_cast<void*>(&ResolveFunction));
    mov(x16, x0);
  }

  // Actually jump/call to x16.
  if (instr->flags & hir::CALL_TAIL) {
    ldr(x1, ptr(sp, (int32_t)StackLayout::GUEST_RET_ADDR));
    add(sp, sp, static_cast<uint32_t>(stack_size()));
    ldp(x29, x30, post_ptr(sp, 16));
    br(x16);
  } else {
    ldr(x1, ptr(sp, (int32_t)StackLayout::GUEST_CALL_RET_ADDR));
    blr(x16);
  }
}

uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  XELOGE("undefined extern call to {:08X} {}", function->address(),
         function->name());
  return 0;
}
void A64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // x16 = target function, x1 = arg0, x2 = arg1
      MovConst(x1, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      MovConst(x2, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      MovConst(x16, reinterpret_cast<uint64_t>(builtin_function->handler()));
      MovConst(x17, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x17);
      // x0 = host return
    }
  } else if (function->behavior() == Function::Behavior::kExtern) {
    auto extern_function = static_cast<const GuestFunction*>(function);
    if (extern_function->extern_handler()) {
      undefined = false;
      ldr(x1, ptr(GetContextReg(),
                  (int32_t)offsetof(ppc::PPCContext, kernel_state)));
      MovConst(x16,
               reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      MovConst(x17, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x17);
      // x0 = host return
    }
  }
  if (undefined) {
    CallNative(UndefinedCallExtern, reinterpret_cast<uint64_t>(function));
  }
}

void A64Emitter::CallNative(void* fn) { CallNativeSafe(fn); }

void A64Emitter::CallNative(uint64_t (*fn)(void* raw_context)) {
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void A64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0)) {
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void A64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                            uint64_t arg0) {
  MovConst(GetNativeParam(0), arg0);
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void A64Emitter::CallNativeSafe(void* fn) {
  // x16 = target function; args in x1/x2/x3; the thunk provides the context
  // as arg 0 (x0) and preserves the caller-saved HIR vector pool.
  MovConst(x16, reinterpret_cast<uint64_t>(fn));
  MovConst(x17, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
  blr(x17);
  // x0 = host return
}

void A64Emitter::SetReturnAddress(uint64_t value) {
  MovConst(x0, value);
  str(x0, ptr(sp, (int32_t)StackLayout::GUEST_CALL_RET_ADDR));
}

XReg A64Emitter::GetNativeParam(uint32_t param) {
  if (param == 0) {
    return x1;
  } else if (param == 1) {
    return x2;
  } else if (param == 2) {
    return x3;
  }
  assert_always();
  return x3;
}

// Important: If you change these, you must update the thunks in
// a64_backend.cc!
XReg A64Emitter::GetContextReg() { return x27; }
XReg A64Emitter::GetMembaseReg() { return x28; }

void A64Emitter::ReloadContext() {
  ldr(GetContextReg(), ptr(sp, (int32_t)StackLayout::GUEST_CTX_HOME));
}

void A64Emitter::ReloadMembase() {
  ldr(GetMembaseReg(),
      ptr(GetContextReg(),
          (int32_t)offsetof(ppc::PPCContext, virtual_membase)));
}

void A64Emitter::MovConst(const XReg& dest, uint64_t v) {
  mov_imm(dest, v);
}

void A64Emitter::MovConst(const WReg& dest, uint32_t v) {
  mov_imm(dest, v);
}

static const vec128_t v_consts[VConst::V_COUNT] = {
    /* VZero          */ vec128f(0.0f),
    /* VOnePS         */ vec128f(1.0f),
    /* VNegativeOnePS */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
    /* VFFFF          */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VSignMaskPS    */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VAbsMaskPS     */
    vec128i(0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu),
    /* VByteSwapMask  */
    vec128i(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu),
    /* VQNaN          */ vec128i(0x7FC00000u),
};

// First location to try and place constants. Must be above the 4 GB
// __PAGEZERO and clear of the guest memory reservations near 0x100000000.
static const uintptr_t kConstDataLocation = 0x600000000;
static const uintptr_t kConstDataSize = sizeof(v_consts);

// Increment the location by this amount for every allocation failure.
static const uintptr_t kConstDataIncrement = 0x00001000;

uintptr_t A64Emitter::PlaceConstData() {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(kConstDataLocation);
  void* mem = nullptr;
  while (!mem) {
    mem = memory::AllocFixed(
        ptr, xe::round_up(kConstDataSize, memory::page_size()),
        memory::AllocationType::kReserveCommit, memory::PageAccess::kReadWrite);

    ptr += kConstDataIncrement;
  }

  std::memcpy(mem, v_consts, sizeof(v_consts));
  memory::Protect(mem, kConstDataSize, memory::PageAccess::kReadOnly, nullptr);

  return reinterpret_cast<uintptr_t>(mem);
}

void A64Emitter::FreeConstData(uintptr_t data) {
  memory::DeallocFixed(reinterpret_cast<void*>(data), 0,
                       memory::DeallocationType::kRelease);
}

uintptr_t A64Emitter::GetVConstPtr(VConst id) const {
  return backend_->emitter_data() + sizeof(vec128_t) * id;
}

void A64Emitter::LoadConstantV(const QReg& dest,
                               const vec128_t& v) {
  if (!v.low && !v.high) {
    // 0000...
    const VReg16B dest_b(dest.getIdx());
    eor(dest_b, dest_b, dest_b);
  } else if (v.low == ~uint64_t(0) && v.high == ~uint64_t(0)) {
    // 1111...
    const VReg16B dest_b(dest.getIdx());
    // Compare-equal of anything with itself is all-ones.
    cmeq(dest_b, dest_b, dest_b);
  } else {
    // TODO(benvanik): a constant pool inside the code cache; 99% are reused.
    MovConst(x0, v.low);
    MovConst(x1, v.high);
    str(x0, ptr(sp, 32));  // Scratch area of the guest frame.
    str(x1, ptr(sp, 40));
    ldr(dest, ptr(sp, 32));
  }
}

void A64Emitter::LoadConstantV(const QReg& dest, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  vec128_t vec = vec128i(x.i, 0, 0, 0);
  if (!x.i) {
    const VReg16B dest_b(dest.getIdx());
    eor(dest_b, dest_b, dest_b);
  } else {
    LoadConstantV(dest, vec);
  }
}

void A64Emitter::LoadConstantV(const QReg& dest, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  vec128_t vec = vec128q(x.i, 0);
  if (!x.i) {
    const VReg16B dest_b(dest.getIdx());
    eor(dest_b, dest_b, dest_b);
  } else {
    LoadConstantV(dest, vec);
  }
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
