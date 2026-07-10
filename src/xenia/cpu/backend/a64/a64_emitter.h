/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_EMITTER_H_
#define XENIA_CPU_BACKEND_A64_A64_EMITTER_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/arena.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_trace_data.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/memory.h"

#include "third_party/xbyak_aarch64/xbyak_aarch64/xbyak_aarch64.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Backend;
class A64CodeCache;

struct EmitFunctionInfo;

// Constants pool available to sequences through GetVConstPtr. Mirrors the x64
// XmmConst table; entries are added as sequences need them.
enum VConst {
  VZero = 0,
  VOnePS,
  VNegativeOnePS,
  VFFFF,
  VSignMaskPS,
  VAbsMaskPS,
  VByteSwapMask,
  VQNaN,
  V0001,
  V3301,
  V3331,
  V3333,
  VPackD3DCOLORSat,
  VPackD3DCOLOR,
  VUnpackD3DCOLOR,
  VUnpackFLOAT16_2,
  VUnpackFLOAT16_4,
  VPackSHORT_Min,
  VPackSHORT_Max,
  VPackSHORT_2,
  VPackSHORT_4,
  VUnpackSHORT_2,
  VUnpackSHORT_4,
  VUnpackSHORT_Overflow,
  VPackUINT_2101010_MinUnpacked,
  VPackUINT_2101010_MaxUnpacked,
  VPackUINT_2101010_MaskUnpacked,
  VPackUINT_2101010_MaskPacked,
  VPackUINT_2101010_Shift,
  VUnpackUINT_2101010_Overflow,
  VPackULONG_4202020_MinUnpacked,
  VPackULONG_4202020_MaxUnpacked,
  VPackULONG_4202020_MaskUnpacked,
  VPackULONG_4202020_PermuteXZ,
  VPackULONG_4202020_PermuteYW,
  VUnpackULONG_4202020_Permute,
  VUnpackULONG_4202020_Overflow,
  VSwapWordMask,
  VPermuteByteMask,
  V_COUNT,
};

// The (misnamed for symmetry with x64) allocator used for the emitter's
// scratch buffer. AutoGrow buffers are plain host heap memory that is never
// executed directly, so protection is disabled.
class XbyakAllocator : public Xbyak_aarch64::Allocator {
 public:
  virtual bool useProtect() const { return false; }
};

class A64Emitter : public Xbyak_aarch64::CodeGenerator {
 public:
  A64Emitter(A64Backend* backend, XbyakAllocator* allocator);
  virtual ~A64Emitter();

  Processor* processor() const { return processor_; }
  A64Backend* backend() const { return backend_; }

  static uintptr_t PlaceConstData();
  static void FreeConstData(uintptr_t data);

  bool Emit(GuestFunction* function, hir::HIRBuilder* builder,
            uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
            void** out_code_address, size_t* out_code_size,
            std::vector<SourceMapEntry>* out_source_map);

 public:
  // Reserved:  sp, x27 (context), x28 (membase), x29 (fp), x30 (lr),
  //            x18 (platform)
  // Scratch:   x0-x2 (+ x16/x17 for emitter-internal jumps/consts)
  //            v0-v2
  // Available: x19-x26 (callee-saved; the HIR GPR pool)
  //            v16-v31 (caller-saved; the HIR FP/vector pool — the
  //            guest-to-host thunks save/restore these around host calls)
  static const int GPR_COUNT = 8;
  static const int FPR_COUNT = 16;

  static void SetupReg(const hir::Value* v, WReg& r) {
    r = WReg(gpr_reg_map_[v->reg.index]);
  }
  static void SetupReg(const hir::Value* v, XReg& r) {
    r = XReg(gpr_reg_map_[v->reg.index]);
  }
  static void SetupReg(const hir::Value* v, SReg& r) {
    r = SReg(fpr_reg_map_[v->reg.index]);
  }
  static void SetupReg(const hir::Value* v, DReg& r) {
    r = DReg(fpr_reg_map_[v->reg.index]);
  }
  static void SetupReg(const hir::Value* v, QReg& r) {
    r = QReg(fpr_reg_map_[v->reg.index]);
  }

  // Looks up (creating on first use) the Xbyak label for a HIR label name.
  // xbyak_aarch64 has no string-labels, so the emitter maintains the mapping
  // per emitted function.
  Xbyak_aarch64::Label& hir_label(const char* name);

  Xbyak_aarch64::Label& epilog_label() { return *epilog_label_; }

  void MarkSourceOffset(const hir::Instr* i);

  void DebugBreak();
  void Trap(uint16_t trap_type = 0);
  void UnimplementedInstr(const hir::Instr* i);

  void Call(const hir::Instr* instr, GuestFunction* function);
  void CallIndirect(const hir::Instr* instr, const WReg& reg);
  void CallExtern(const hir::Instr* instr, const Function* function);
  void CallNative(void* fn);
  void CallNative(uint64_t (*fn)(void* raw_context));
  void CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0));
  void CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                  uint64_t arg0);
  void CallNativeSafe(void* fn);
  void SetReturnAddress(uint64_t value);

  XReg GetNativeParam(uint32_t param);

  XReg GetContextReg();
  XReg GetMembaseReg();
  void ReloadContext();
  void ReloadMembase();

  // Moves a 64bit immediate into a register.
  void MovConst(const XReg& dest, uint64_t v);
  void MovConst(const WReg& dest, uint32_t v);

  uintptr_t GetVConstPtr(VConst id) const;
  void LoadVConst(const QReg& dest, VConst id);
  void LoadConstantV(const QReg& dest, float v);
  void LoadConstantV(const QReg& dest, double v);
  void LoadConstantV(const QReg& dest, const vec128_t& v);

  FunctionDebugInfo* debug_info() const { return debug_info_; }

  size_t stack_size() const { return stack_size_; }

 protected:
  void* Emplace(const EmitFunctionInfo& func_info,
                GuestFunction* function = nullptr);
  bool Emit(hir::HIRBuilder* builder, EmitFunctionInfo& func_info);

 protected:
  Processor* processor_ = nullptr;
  A64Backend* backend_ = nullptr;
  A64CodeCache* code_cache_ = nullptr;
  XbyakAllocator* allocator_ = nullptr;

  Xbyak_aarch64::Label* epilog_label_ = nullptr;
  std::unordered_map<std::string, Xbyak_aarch64::Label> hir_labels_;

  hir::Instr* current_instr_ = nullptr;

  FunctionDebugInfo* debug_info_ = nullptr;
  uint32_t debug_info_flags_ = 0;
  FunctionTraceData* trace_data_ = nullptr;
  Arena source_map_arena_;

  size_t stack_size_ = 0;

  static const uint32_t gpr_reg_map_[GPR_COUNT];
  static const uint32_t fpr_reg_map_[FPR_COUNT];
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_EMITTER_H_
